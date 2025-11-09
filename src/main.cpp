#include <bits/stdc++.h>
#include <filesystem>
#include <iomanip>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

std::string perms_to_string(fs::perms p) {
    auto bit = [&](fs::perms b, char c){ return ( (p & b) != fs::perms::none ) ? c : '-'; };
    std::string s;
    s += bit(fs::perms::owner_read,  'r');
    s += bit(fs::perms::owner_write, 'w');
    s += bit(fs::perms::owner_exec,  'x');
    s += bit(fs::perms::group_read,  'r');
    s += bit(fs::perms::group_write, 'w');
    s += bit(fs::perms::group_exec,  'x');
    s += bit(fs::perms::others_read, 'r');
    s += bit(fs::perms::others_write,'w');
    s += bit(fs::perms::others_exec, 'x');
    return s;
}

std::string human_size(uintmax_t bytes) {
    const char* suffix[] = {"B","KB","MB","GB","TB"};
    double c = bytes; int i = 0;
    while (c >= 1024 && i < 4) { c /= 1024; ++i; }
    std::ostringstream os; os<<std::fixed<<std::setprecision((i==0)?0:1)<<c<<suffix[i];
    return os.str();
}

std::string owner_name(const fs::path& p) {
    struct stat st{};
    if (lstat(p.c_str(), &st) == -1) return "?";
    passwd* pw = getpwuid(st.st_uid);
    group*  gr = getgrgid(st.st_gid);
    std::ostringstream os;
    os << (pw?pw->pw_name:std::to_string(st.st_uid)) << ":" << (gr?gr->gr_name:std::to_string(st.st_gid));
    return os.str();
}

void print_entry(const fs::directory_entry& e) {
    auto st = fs::symlink_status(e.path());
    auto type = fs::is_directory(st) ? 'd' : fs::is_symlink(st) ? 'l' : '-';
    auto perm = perms_to_string(st.permissions());
    uintmax_t size = 0;
    if (fs::is_regular_file(st)) {
        std::error_code ec; size = fs::file_size(e.path(), ec);
    }

    auto ftime = fs::last_write_time(e.path());
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm = *std::localtime(&tt);

    std::cout << type << perm << " "
              << std::setw(10) << owner_name(e.path()) << " "
              << std::setw(8) << human_size(size) << "  "
              << std::put_time(&tm, "%Y-%m-%d %H:%M") << "  "
              << e.path().filename().string() << "\n";
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size()!=b.size()) return false;
    for (size_t i=0;i<a.size();++i)
        if (std::tolower(a[i]) != std::tolower(b[i])) return false;
    return true;
}

bool contains_icase(const std::string& hay, const std::string& needle) {
    auto H = hay; auto N = needle;
    std::transform(H.begin(),H.end(),H.begin(),::tolower);
    std::transform(N.begin(),N.end(),N.begin(),::tolower);
    return H.find(N) != std::string::npos;
}

void help() {
    std::cout <<
R"(Commands:
  ls [path]                 list directory
  cd <path>                 change directory
  pwd                       print working directory
  tree [depth]              tree view (default depth=2)
  mkdir <name>              create directory
  touch <name>              create empty file (or update mtime)
  rm <path>                 remove file
  rmdir <path>              remove directory recursively
  cp <src> <dst>            copy (file or directory)
  mv <src> <dst>            move/rename
  open <file>               print file (first 200 lines)
  search <name-frag>        search recursively by name
  chmod <octal> <path>      set permissions (e.g., 755)
  perms [path]              show permissions entries
  help                      show this help
  exit                      quit
)";
}

void list_dir(const fs::path& p) {
    std::vector<fs::directory_entry> items;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(p, ec)) items.push_back(e);
    if (ec) { std::cerr << "ls: " << ec.message() << "\n"; return; }
    std::sort(items.begin(), items.end(),
              [](const auto& a, const auto& b){
                  if (a.is_directory() != b.is_directory())
                      return a.is_directory() && !b.is_directory();
                  return a.path().filename().string() < b.path().filename().string();
              });
    for (auto& e : items) print_entry(e);
}

void tree(const fs::path& root, int max_depth, int depth=0) {
    if (depth > max_depth) return;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root, ec)) {
        if (ec) { std::cerr << "tree: " << ec.message() << "\n"; return; }
        for (int i=0;i<depth;i++) std::cout << "  ";
        std::cout << "|- " << e.path().filename().string() << "\n";
        if (e.is_directory()) tree(e.path(), max_depth, depth+1);
    }
}

void touch(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p)) {
        std::ofstream f(p);
        if (!f) { std::cerr<<"touch: cannot create\n"; return; }
    } else {
        auto now = fs::file_time_type::clock::now();   // fixed
        fs::last_write_time(p, now, ec);
        if (ec) { std::cerr<<"touch: "<<ec.message()<<"\n"; }
    }
}

void copy_any(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    if (fs::is_directory(src)) {
        fs::create_directories(dst, ec);
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) std::cerr << "cp: " << ec.message() << "\n";
}

void show_file(const fs::path& p) {
    std::ifstream in(p);
    if (!in) { std::cerr << "open: cannot open file\n"; return; }
    std::string line; int n=0;
    while (n<200 && std::getline(in,line)) { std::cout << line << "\n"; ++n; }
    if (!in.eof()) std::cout << "...(truncated)\n";
}

void search_name(const fs::path& root, const std::string& pat) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) { std::cerr << "search: " << ec.message() << "\n"; break; }
        if (contains_icase(it->path().filename().string(), pat)) {
            std::cout << it->path().string() << "\n";
        }
    }
}

void show_perms(const fs::path& p) {
    if (!fs::exists(p)) { std::cerr<<"perms: path not found\n"; return; }
    if (fs::is_directory(p)) {
        for (auto& e : fs::directory_iterator(p)) print_entry(e);
    } else {
        print_entry(fs::directory_entry(p));
    }
}

void chmod_octal(const fs::path& p, const std::string& oct) {
    if (oct.size()<3 || oct.size()>4 || !std::all_of(oct.begin(),oct.end(),::isdigit)) {
        std::cerr << "chmod: use octal like 755 or 0644\n"; return;
    }
    unsigned mode = std::stoul(oct, nullptr, 8);
    fs::perms perm = static_cast<fs::perms>(mode);
    std::error_code ec;
    fs::permissions(p, perm, ec);
    if (ec) std::cerr << "chmod: " << ec.message() << "\n";
}

int main() {
    std::cout << "File Explorer (C++17, Linux/WSL)\n";
    help();
    fs::path cwd = fs::current_path();

    std::string line;
    while (true) {
        std::cout << "\n[" << cwd.string() << "]$ ";
        if (!std::getline(std::cin, line)) break;
        std::istringstream iss(line);
        std::string cmd; iss >> cmd;
        if (cmd.empty()) continue;

        try {
            if (cmd=="ls") {
                std::string p; iss >> p;
                list_dir(p.empty()?cwd:fs::path(p).is_absolute()?fs::path(p):(cwd/p));
            } else if (cmd=="pwd") {
                std::cout << cwd.string() << "\n";
            } else if (cmd=="cd") {
                std::string p; iss >> p;
                if (p.empty()) { std::cerr<<"cd: path required\n"; continue; }
                fs::path np = fs::path(p).is_absolute()?fs::path(p):(cwd/p);
                if (fs::exists(np) && fs::is_directory(np)) { cwd = fs::canonical(np); fs::current_path(cwd); }
                else std::cerr<<"cd: not a directory\n";
            } else if (cmd=="tree") {
                int d=2; iss>>d; if (d<0) d=0; tree(cwd,d);
            } else if (cmd=="mkdir") {
                std::string n; iss>>n; if(n.empty()){std::cerr<<"mkdir: name required\n";continue;}
                std::error_code ec; fs::create_directories(cwd/n, ec); if(ec) std::cerr<<"mkdir: "<<ec.message()<<"\n";
            } else if (cmd=="touch") {
                std::string n; iss>>n; if(n.empty()){std::cerr<<"touch: name required\n";continue;}
                touch(cwd/n);
            } else if (cmd=="rm") {
                std::string p; iss>>p; if(p.empty()){std::cerr<<"rm: path required\n";continue;}
                std::error_code ec; fs::remove(cwd/p, ec); if(ec) std::cerr<<"rm: "<<ec.message()<<"\n";
            } else if (cmd=="rmdir") {
                std::string p; iss>>p; if(p.empty()){std::cerr<<"rmdir: path required\n";continue;}
                std::error_code ec; fs::remove_all(cwd/p, ec); if(ec) std::cerr<<"rmdir: "<<ec.message()<<"\n";
            } else if (cmd=="cp") {
                std::string s,d; iss>>s>>d; if(d.empty()){std::cerr<<"cp: src dst required\n";continue;}
                copy_any(fs::path(s).is_absolute()?fs::path(s):(cwd/s),
                         fs::path(d).is_absolute()?fs::path(d):(cwd/d));
            } else if (cmd=="mv") {
                std::string s,d; iss>>s>>d; if(d.empty()){std::cerr<<"mv: src dst required\n";continue;}
                std::error_code ec;
                fs::rename(fs::path(s).is_absolute()?fs::path(s):(cwd/s),
                           fs::path(d).is_absolute()?fs::path(d):(cwd/d), ec);
                if(ec) std::cerr<<"mv: "<<ec.message()<<"\n";
            } else if (cmd=="open") {
                std::string f; iss>>f; if(f.empty()){std::cerr<<"open: file required\n";continue;}
                show_file(fs::path(f).is_absolute()?fs::path(f):(cwd/f));
            } else if (cmd=="search") {
                std::string pat; iss>>pat; if(pat.empty()){std::cerr<<"search: name required\n";continue;}
                search_name(cwd, pat);
            } else if (cmd=="chmod") {
                std::string oct, p; iss>>oct>>p;
                if(p.empty()){std::cerr<<"chmod: <octal> <path>\n";continue;}
                chmod_octal(fs::path(p).is_absolute()?fs::path(p):(cwd/p), oct);
            } else if (cmd=="perms") {
                std::string p; iss>>p; fs::path t = p.empty()?cwd:(fs::path(p).is_absolute()?fs::path(p):(cwd/p));
                show_perms(t);
            } else if (cmd=="help") {
                help();
            } else if (cmd=="exit" || cmd=="quit") {
                break;
            } else {
                std::cerr << "Unknown command. Type 'help'.\n";
            }
        } catch(const std::exception& ex) {
            std::cerr << "error: " << ex.what() << "\n";
        }
    }
    return 0;
}

