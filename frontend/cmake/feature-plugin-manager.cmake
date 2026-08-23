find_package(nlohmann_json 3.11 REQUIRED)

target_sources(
  nexus-broadcast-studio
  PRIVATE
    plugin-manager/PluginManager.cpp
    plugin-manager/PluginManager.hpp
    plugin-manager/PluginManagerWindow.cpp
    plugin-manager/PluginManagerWindow.hpp
)

target_link_libraries(nexus-broadcast-studio PRIVATE nlohmann_json::nlohmann_json)