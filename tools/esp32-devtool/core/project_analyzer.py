import os
import re
from typing import Dict, List, Set

class ProjectAnalyzer:
    def __init__(self, project_root: str):
        self.project_root = project_root
        self._referenced_ports = {}

    def get_referenced_ports(self) -> Dict[str, List[str]]:
        """
        Returns a dict mapping port name to list of files referencing it.
        Example: {"COM3": ["sdkconfig", "main/boards/esp32s3/config.h"]}
        """
        self._referenced_ports = {}
        self._scan_sdkconfig()
        self._scan_board_configs()
        return self._referenced_ports

    def _add_reference(self, port: str, file_path: str):
        # Clean up port string
        port = port.strip('"\' \t')
        if port not in self._referenced_ports:
            self._referenced_ports[port] = []
        rel_path = os.path.relpath(file_path, self.project_root)
        if rel_path not in self._referenced_ports[port]:
            self._referenced_ports[port].append(rel_path)

    def _scan_sdkconfig(self):
        # Scan sdkconfig and sdkconfig.defaults*
        for file in os.listdir(self.project_root):
            if file.startswith('sdkconfig'):
                path = os.path.join(self.project_root, file)
                if os.path.isfile(path):
                    self._parse_file_for_ports(path, r'CONFIG_.*?_PORT="?(COM\d+|/dev/\S+)"?')
                    
    def _scan_board_configs(self):
        boards_dir = os.path.join(self.project_root, 'main', 'boards')
        if not os.path.exists(boards_dir):
            return
            
        for root, _, files in os.walk(boards_dir):
            for file in files:
                if file.endswith('.h') or file == 'config.h':
                    path = os.path.join(root, file)
                    # Look for #define *_PORT "COMX" or *_UART "COMX"
                    self._parse_file_for_ports(path, r'#define\s+.*?_PORT\s+"?(COM\d+|/dev/\S+)"?')
                    self._parse_file_for_ports(path, r'#define\s+.*?_UART\s+"?(COM\d+|/dev/\S+)"?')

    def _parse_file_for_ports(self, path: str, pattern: str):
        try:
            with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    match = re.search(pattern, line)
                    if match:
                        port = match.group(1)
                        self._add_reference(port, path)
        except Exception:
            pass
