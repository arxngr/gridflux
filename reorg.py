import os
import shutil
import re

base_dir = "/home/ardinugraha/Documents/Workshops/gridflux/src"

file_map = {
    "wm.c": "core/wm.c",
    "arrange.c": "core/arrange.c",
    "workspace.c": "core/workspace.c",
    "window.c": "core/window.c",
    "events.c": "core/events.c",
    "debug.c": "core/debug.c",
    "internal.h": "core/internal.h",
    "types.h": "core/types.h",
    "window_manager.h": "core/wm.h",
    
    "ipc.h": "ipc/ipc.h",
    "ipc_command.c": "ipc/ipc_command.c",
    "ipc_command.h": "ipc/ipc_command.h",
    
    "list.c": "utils/list.c",
    "list.h": "utils/list.h",
    "memory.c": "utils/memory.c",
    "memory.h": "utils/memory.h",
    "logger.c": "utils/logger.c",
    "logger.h": "utils/logger.h",
    "file.c": "utils/file.c",
    "file.h": "utils/file.h",
    
    "config.c": "config/config.c",
    "config.h": "config/config.h",
    "layout.c": "config/layout.c",
    "layout.h": "config/layout.h",
    
    "main.c": "apps/main.c",
    "client.c": "apps/client.c",
    "gui.c": "apps/gui.c",
    
    "platform_compat.h": "platform/platform_compat.h",
}

for root, dirs, files in os.walk(base_dir):
    for f in files:
        if f.endswith('.c') or f.endswith('.h'):
            old_rel = os.path.relpath(os.path.join(root, f), base_dir)
            if old_rel not in file_map:
                file_map[old_rel] = old_rel

def resolve_old_include(old_file_rel, include_str):
    current_dir = os.path.dirname(old_file_rel)
    import posixpath
    resolved = posixpath.normpath(posixpath.join(current_dir, include_str))
    return resolved

def get_new_include(new_file_rel, new_target_rel):
    import posixpath
    curr_dir = posixpath.dirname(new_file_rel)
    target_dir = posixpath.dirname(new_target_rel)
    if curr_dir == target_dir:
        return posixpath.basename(new_target_rel)
    
    curr_parts = curr_dir.split('/') if curr_dir else []
    target_parts = target_dir.split('/') if target_dir else []
    
    i = 0
    while i < len(curr_parts) and i < len(target_parts) and curr_parts[i] == target_parts[i]:
        i += 1
        
    up = ['..'] * (len(curr_parts) - i)
    down = target_parts[i:]
    
    rel_parts = up + down + [posixpath.basename(new_target_rel)]
    return "/".join(rel_parts)

old_to_new = {old: new for old, new in file_map.items()}
# Let's add window_manager.h specifically
old_to_new['window_manager.h'] = 'core/wm.h'

out_contents = {}
for old_rel, new_rel in file_map.items():
    old_abs = os.path.join(base_dir, old_rel)
    with open(old_abs, 'r') as f:
        content = f.read()
        
    def replacer(match):
        include_str = match.group(1)
        old_target = resolve_old_include(old_rel, include_str)
        # if the file relies on implicit include_directories(src), its resolved might be wrong
        if old_target not in old_to_new:
            # Let's check if the bare include string exists in src/
            if include_str in old_to_new:
                old_target = include_str
            else:
                return match.group(0) # Not found in our map (system include?)
        
        new_target = old_to_new[old_target]
        new_include_str = get_new_include(new_rel, new_target)
        return f'#include "{new_include_str}"'
    
    new_content = re.sub(r'#include\s+"([^"]+)"', replacer, content)
    out_contents[new_rel] = new_content

new_base = "/home/ardinugraha/Documents/Workshops/gridflux/src_new"
if os.path.exists(new_base):
    shutil.rmtree(new_base)
os.makedirs(new_base)

for old_rel, new_rel in file_map.items():
    new_abs = os.path.join(new_base, new_rel)
    os.makedirs(os.path.dirname(new_abs), exist_ok=True)
    with open(new_abs, 'w') as f:
        f.write(out_contents[new_rel])

shutil.move("/home/ardinugraha/Documents/Workshops/gridflux/src", "/home/ardinugraha/Documents/Workshops/gridflux/src_old")
shutil.move("/home/ardinugraha/Documents/Workshops/gridflux/src_new", "/home/ardinugraha/Documents/Workshops/gridflux/src")

# Update CMakeLists.txt
cmake_path = '/home/ardinugraha/Documents/Workshops/gridflux/CMakeLists.txt'
with open(cmake_path, 'r') as f:
    cmake = f.read()

cmake = cmake.replace('file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "src/*.c")\\nlist(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/client.c")\\nlist(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/gui.c")',
'''file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
    "src/core/*.c"
    "src/utils/*.c"
    "src/config/*.c"
    "src/ipc/*.c"
)
list(APPEND COMMON_SOURCES "src/apps/main.c")''')

# Hardcoded replacement
cmake = cmake.replace('file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "src/*.c")\nlist(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/client.c")\nlist(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/gui.c")',
'''file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
    "src/core/*.c"
    "src/utils/*.c"
    "src/config/*.c"
    "src/ipc/*.c"
)
list(APPEND COMMON_SOURCES "src/apps/main.c")''')

# For the hardcoded paths in add_executable
cmake = cmake.replace('src/wm.c', 'src/core/wm.c')
cmake = cmake.replace('src/arrange.c', 'src/core/arrange.c')
cmake = cmake.replace('src/workspace.c', 'src/core/workspace.c')
cmake = cmake.replace('src/window.c', 'src/core/window.c')
cmake = cmake.replace('src/events.c', 'src/core/events.c')
cmake = cmake.replace('src/debug.c', 'src/core/debug.c')

cmake = cmake.replace('src/client.c', 'src/apps/client.c')
cmake = cmake.replace('src/gui.c', 'src/apps/gui.c')

cmake = cmake.replace('src/ipc_command.c', 'src/ipc/ipc_command.c')

cmake = cmake.replace('src/list.c', 'src/utils/list.c')
cmake = cmake.replace('src/memory.c', 'src/utils/memory.c')
cmake = cmake.replace('src/logger.c', 'src/utils/logger.c')
cmake = cmake.replace('src/file.c', 'src/utils/file.c')

cmake = cmake.replace('src/config.c', 'src/config/config.c')
cmake = cmake.replace('src/layout.c', 'src/config/layout.c')

with open(cmake_path, 'w') as f:
    f.write(cmake)

print("Reorg successful.")
