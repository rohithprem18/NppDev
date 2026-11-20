// NppDev's LeetCode runner for C++ (included before your file): what LeetCode's C++
// environment already has, so a file with only `class Solution` compiles as it does there.
#include <bits/stdc++.h>
using namespace std;

#ifndef NPPDEV_HAS_LISTNODE
struct ListNode {
    int val;
    ListNode *next;
    ListNode() : val(0), next(nullptr) {}
    ListNode(int x) : val(x), next(nullptr) {}
    ListNode(int x, ListNode *next) : val(x), next(next) {}
};
#endif

#ifndef NPPDEV_HAS_TREENODE
struct TreeNode {
    int val;
    TreeNode *left;
    TreeNode *right;
    TreeNode() : val(0), left(nullptr), right(nullptr) {}
    TreeNode(int x) : val(x), left(nullptr), right(nullptr) {}
    TreeNode(int x, TreeNode *left, TreeNode *right) : val(x), left(left), right(right) {}
};
#endif
