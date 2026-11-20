// NppDev's LeetCode runner for C++ (included after your file): reads one argument per input
// line in LeetCode's format, calls the Solution method, prints the result like LeetCode.
// Argument types are taken from the method itself, so any mix of int, long long, double,
// bool, char, string, vector<...>, ListNode* and TreeNode* works.
namespace nppdev {

struct In {
    const string &s;
    size_t i = 0;
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    char peek() { ws(); return i < s.size() ? s[i] : '\0'; }
    bool eat(char c) { if (peek() == c) { i++; return true; } return false; }
    bool null_() { ws(); if (s.compare(i, 4, "null") == 0) { i += 4; return true; } return false; }
    string number() {
        ws();
        size_t b = i;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || strchr("+-.eE", s[i]))) i++;
        return s.substr(b, i - b);
    }
};

inline void read(In &in, int &x) { x = in.null_() ? 0 : (int)stoll(in.number()); }
inline void read(In &in, long &x) { x = in.null_() ? 0 : (long)stoll(in.number()); }
inline void read(In &in, long long &x) { x = in.null_() ? 0 : stoll(in.number()); }
inline void read(In &in, unsigned &x) { x = (unsigned)stoull(in.number()); }
inline void read(In &in, unsigned long long &x) { x = stoull(in.number()); }
inline void read(In &in, double &x) { x = stod(in.number()); }
inline void read(In &in, float &x) { x = stof(in.number()); }
inline void read(In &in, bool &x) {
    in.ws();
    x = in.s.compare(in.i, 4, "true") == 0;
    in.i += x ? 4 : 5;
}
inline void read(In &in, string &x) {
    x.clear();
    if (!in.eat('"')) { x = in.number(); return; }
    while (in.i < in.s.size() && in.s[in.i] != '"') {
        char c = in.s[in.i++];
        if (c == '\\' && in.i < in.s.size()) {
            char e = in.s[in.i++];
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
        }
        x += c;
    }
    in.i++;
}
inline void read(In &in, char &x) { string t; read(in, t); x = t.empty() ? '\0' : t[0]; }
template <class T> void read(In &in, vector<T> &v) {
    v.clear();
    in.eat('[');
    while (in.peek() != ']' && in.peek() != '\0') {
        T x{};
        read(in, x);
        v.push_back(x);
        in.eat(',');
    }
    in.eat(']');
}
inline void read(In &in, ListNode *&head) {
    vector<int> v;
    read(in, v);
    ListNode dummy, *cur = &dummy;
    for (int x : v) { cur->next = new ListNode(x); cur = cur->next; }
    head = dummy.next;
}
inline void read(In &in, TreeNode *&root) {
    vector<optional<int>> v;
    in.eat('[');
    while (in.peek() != ']' && in.peek() != '\0') {
        if (in.null_()) v.push_back(nullopt);
        else { int x; read(in, x); v.push_back(x); }
        in.eat(',');
    }
    in.eat(']');
    root = nullptr;
    if (v.empty() || !v[0]) return;
    root = new TreeNode(*v[0]);
    queue<TreeNode *> q;
    q.push(root);
    size_t i = 1;
    while (!q.empty() && i < v.size()) {
        TreeNode *n = q.front();
        q.pop();
        if (i < v.size() && v[i]) { n->left = new TreeNode(*v[i]); q.push(n->left); }
        i++;
        if (i < v.size() && v[i]) { n->right = new TreeNode(*v[i]); q.push(n->right); }
        i++;
    }
}

inline void print(ostream &o, int x) { o << x; }
inline void print(ostream &o, long x) { o << x; }
inline void print(ostream &o, long long x) { o << x; }
inline void print(ostream &o, unsigned x) { o << x; }
inline void print(ostream &o, unsigned long long x) { o << x; }
inline void print(ostream &o, double x) { o << fixed << setprecision(5) << x; }
inline void print(ostream &o, float x) { print(o, (double)x); }
inline void print(ostream &o, bool x) { o << (x ? "true" : "false"); }
inline void print(ostream &o, char x) { o << '"' << x << '"'; }
inline void print(ostream &o, const string &x) { o << '"' << x << '"'; }
inline void print(ostream &o, const char *x) { o << '"' << x << '"'; }
inline void print(ostream &o, ListNode *n) {
    o << '[';
    for (bool first = true; n; n = n->next, first = false) o << (first ? "" : ",") << n->val;
    o << ']';
}
inline void print(ostream &o, TreeNode *root) {
    vector<string> out;
    queue<TreeNode *> q;
    q.push(root);
    while (!q.empty()) {
        TreeNode *n = q.front();
        q.pop();
        if (!n) { out.push_back("null"); continue; }
        out.push_back(to_string(n->val));
        q.push(n->left);
        q.push(n->right);
    }
    while (!out.empty() && out.back() == "null") out.pop_back();
    o << '[';
    for (size_t i = 0; i < out.size(); i++) o << (i ? "," : "") << out[i];
    o << ']';
}
template <class T> void print(ostream &o, const vector<T> &v) {
    o << '[';
    for (size_t i = 0; i < v.size(); i++) { if (i) o << ','; print(o, (const T &)v[i]); }
    o << ']';
}
inline void print(ostream &o, const vector<bool> &v) {
    o << '[';
    for (size_t i = 0; i < v.size(); i++) o << (i ? "," : "") << (v[i] ? "true" : "false");
    o << ']';
}

template <class T> T parse(const string &line) {
    In in{line};
    T x{};
    read(in, x);
    return x;
}

template <class Obj, class R, class... A, size_t... I>
void call(Obj &obj, R (Obj::*f)(A...), const vector<string> &lines, size_t at, index_sequence<I...>) {
    tuple<decay_t<A>...> args{parse<decay_t<A>>(lines[at + I])...};
    if constexpr (is_void_v<R>) {
        apply([&](auto &...a) { (obj.*f)(a...); }, args);
        if constexpr (sizeof...(A) > 0) print(cout, get<0>(args));
    } else {
        auto r = apply([&](auto &...a) { return (obj.*f)(a...); }, args);
        print(cout, r);
    }
    cout << '\n';
}

template <class Obj, class R, class... A> void run(R (Obj::*f)(A...), const vector<string> &lines) {
    constexpr size_t n = sizeof...(A);
    if (lines.size() < n) {
        cerr << "NppDev: the method takes " << n << " argument(s), one per input line; the input has " << lines.size() << ".\n";
        exit(1);
    }
    for (size_t at = 0; at + n <= lines.size(); at += (n ? n : 1)) {
        Obj obj;
        call(obj, f, lines, at, make_index_sequence<n>{});
        if (n == 0) break;
    }
}
template <class Obj, class R, class... A> void run(R (Obj::*f)(A...) const, const vector<string> &lines) {
    run(reinterpret_cast<R (Obj::*)(A...)>(f), lines);
}

inline vector<string> input_lines() {
    vector<string> lines;
    string l;
    while (getline(cin, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.find_first_not_of(" \t") != string::npos) lines.push_back(l);
    }
    return lines;
}

} // namespace nppdev
