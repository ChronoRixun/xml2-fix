#pragma once
#include <cstdint>
#include <vector>
namespace loader_placements_rules {
constexpr std::uint32_t limit=1024;
// C2 counts every <inst> in the tree, including nested groups. Count before
// invoking 0x461d10, whose equality guard silently truncates the native arrays.
template<class Node,class First,class Next,class IsInst>
bool fits(Node root, First first, Next next, IsInst is_inst, std::uint32_t& count) {
 count=0; std::vector<Node> todo;
 if(root)todo.push_back(root);
 while(!todo.empty()) {
  const auto node=todo.back();todo.pop_back();
  if(is_inst(node) && ++count>limit)return false;
  for(auto child=first(node);child;child=next(node,child))todo.push_back(child);
 }
 return true;
}
}
