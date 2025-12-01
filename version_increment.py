"""
Version increment script for PlatformIO
Automatically increments patch version on each build
"""

Import("env")

import os
import re

def increment_version(source, target, env):
    """Increment the patch version number in config.h"""
    
    config_path = os.path.join(env.subst("$PROJECT_DIR"), "include", "config.h")
    
    if not os.path.exists(config_path):
        print("Warning: config.h not found, skipping version increment")
        return
    
    with open(config_path, 'r') as f:
        content = f.read()
    
    # Match version string like "1.0.0"
    match = re.search(r'#define FIRMWARE_VERSION "(\d+)\.(\d+)\.(\d+)"', content)
    
    if match:
        major = int(match.group(1))
        minor = int(match.group(2))
        patch = int(match.group(3))
        
        # Increment patch version
        new_patch = patch + 1
        new_version = f'{major}.{minor}.{new_patch}'
        
        # Replace in content
        new_content = re.sub(
            r'#define FIRMWARE_VERSION "\d+\.\d+\.\d+"',
            f'#define FIRMWARE_VERSION "{new_version}"',
            content
        )
        
        with open(config_path, 'w') as f:
            f.write(new_content)
        
        print(f"Version incremented to {new_version}")
    else:
        print("Warning: Could not find version string in config.h")

# Uncomment the following line to enable auto-increment on each build
# env.AddPreAction("buildprog", increment_version)

