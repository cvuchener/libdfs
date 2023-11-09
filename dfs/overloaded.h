#ifndef DFS_OVERLOADED_H
#define DFS_OVERLOADED_H

namespace dfs {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

} // namespace dfs

#endif
