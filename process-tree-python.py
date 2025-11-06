#!/usr/bin/env python3
"""
Process Tree Visualizer - Python Implementation
Created by: Michael Semera

A system utility that displays running processes in a hierarchical tree structure,
showing parent-child relationships with detailed process information.

This implementation uses the psutil library for cross-platform process information
and direct system calls where applicable.
"""

import psutil
import sys
import argparse
import time
from datetime import datetime
from typing import Dict, List, Optional, Set
from dataclasses import dataclass
from collections import defaultdict

try:
    from colorama import init, Fore, Style
    init(autoreset=True)
    COLOR_SUPPORT = True
except ImportError:
    COLOR_SUPPORT = False
    # Fallback - no colors
    class Fore:
        RED = GREEN = YELLOW = BLUE = CYAN = MAGENTA = WHITE = RESET = ""
    class Style:
        BRIGHT = DIM = RESET_ALL = ""


@dataclass
class ProcessInfo:
    """Data structure representing a process"""
    pid: int
    ppid: int
    name: str
    username: str
    status: str
    cpu_percent: float
    memory_mb: float
    num_threads: int
    create_time: float
    cmdline: str
    children: List['ProcessInfo'] = None
    
    def __post_init__(self):
        if self.children is None:
            self.children = []
    
    def format_memory(self) -> str:
        """Format memory in human-readable form"""
        if self.memory_mb >= 1024:
            return f"{self.memory_mb / 1024:.1f}GB"
        return f"{self.memory_mb:.1f}MB"
    
    def format_uptime(self) -> str:
        """Format process uptime"""
        uptime = time.time() - self.create_time
        hours = int(uptime // 3600)
        minutes = int((uptime % 3600) // 60)
        if hours > 0:
            return f"{hours}h{minutes}m"
        return f"{minutes}m"


class ProcessTree:
    """
    Main class for building and displaying the process tree.
    Uses psutil for cross-platform process information gathering.
    """
    
    def __init__(self, show_resources: bool = False, verbose: bool = False):
        self.processes: Dict[int, ProcessInfo] = {}
        self.root_processes: List[ProcessInfo] = []
        self.show_resources = show_resources
        self.verbose = verbose
        self.total_processes = 0
        self.collection_errors = 0
        
    def collect_processes(self) -> None:
        """
        Collect information about all running processes.
        Uses psutil for cross-platform compatibility.
        """
        print(f"{Fore.CYAN}Collecting process information...{Style.RESET_ALL}")
        
        # Get all process IDs first
        try:
            pids = psutil.pids()
            self.total_processes = len(pids)
        except Exception as e:
            print(f"{Fore.RED}Error getting process list: {e}{Style.RESET_ALL}")
            return
        
        # Collect information for each process
        for pid in pids:
            try:
                proc = psutil.Process(pid)
                
                # Get process information with error handling for each field
                try:
                    name = proc.name()
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    name = "<unknown>"
                
                try:
                    ppid = proc.ppid()
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    ppid = 0
                
                try:
                    username = proc.username()
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    username = "<unknown>"
                
                try:
                    status = proc.status()
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    status = "unknown"
                
                try:
                    cpu_percent = proc.cpu_percent(interval=0.1) if self.show_resources else 0.0
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    cpu_percent = 0.0
                
                try:
                    memory_mb = proc.memory_info().rss / (1024 * 1024)
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    memory_mb = 0.0
                
                try:
                    num_threads = proc.num_threads()
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    num_threads = 0
                
                try:
                    create_time = proc.create_time()
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    create_time = time.time()
                
                try:
                    cmdline = ' '.join(proc.cmdline())
                    if not cmdline:
                        cmdline = name
                except (psutil.AccessDenied, psutil.NoSuchProcess):
                    cmdline = name
                
                # Create ProcessInfo object
                process_info = ProcessInfo(
                    pid=pid,
                    ppid=ppid,
                    name=name,
                    username=username,
                    status=status,
                    cpu_percent=cpu_percent,
                    memory_mb=memory_mb,
                    num_threads=num_threads,
                    create_time=create_time,
                    cmdline=cmdline
                )
                
                self.processes[pid] = process_info
                
            except (psutil.NoSuchProcess, psutil.AccessDenied) as e:
                self.collection_errors += 1
                if self.verbose:
                    print(f"{Fore.YELLOW}Warning: Cannot access PID {pid}: {e}{Style.RESET_ALL}")
                continue
            except Exception as e:
                self.collection_errors += 1
                if self.verbose:
                    print(f"{Fore.RED}Error processing PID {pid}: {e}{Style.RESET_ALL}")
                continue
        
        print(f"{Fore.GREEN}Collected {len(self.processes)} processes{Style.RESET_ALL}")
        if self.collection_errors > 0:
            print(f"{Fore.YELLOW}({self.collection_errors} processes inaccessible){Style.RESET_ALL}")
    
    def build_tree(self) -> None:
        """
        Build the hierarchical tree structure by linking children to parents.
        """
        # First pass: link children to parents
        for pid, proc in self.processes.items():
            if proc.ppid in self.processes:
                parent = self.processes[proc.ppid]
                parent.children.append(proc)
            else:
                # No parent found - this is a root process
                self.root_processes.append(proc)
        
        # Sort children by PID for consistent display
        for proc in self.processes.values():
            proc.children.sort(key=lambda p: p.pid)
        
        # Sort root processes by PID
        self.root_processes.sort(key=lambda p: p.pid)
    
    def display_tree(self, process: ProcessInfo = None, prefix: str = "", 
                     is_last: bool = True, visited: Set[int] = None) -> None:
        """
        Recursively display the process tree with proper indentation.
        
        Args:
            process: The process to display
            prefix: The current indentation prefix
            is_last: Whether this is the last child of its parent
            visited: Set of already visited PIDs to prevent infinite loops
        """
        if visited is None:
            visited = set()
        
        # Display all root processes if no specific process given
        if process is None:
            for i, root in enumerate(self.root_processes):
                is_last_root = (i == len(self.root_processes) - 1)
                self.display_tree(root, "", is_last_root, visited)
            return
        
        # Prevent infinite loops from circular references
        if process.pid in visited:
            return
        visited.add(process.pid)
        
        # Build the display line
        connector = "└── " if is_last else "├── "
        
        # Color code based on process status
        if process.status == "running":
            name_color = Fore.GREEN
        elif process.status == "sleeping":
            name_color = Fore.CYAN
        elif process.status in ["stopped", "zombie"]:
            name_color = Fore.RED
        else:
            name_color = Fore.WHITE
        
        # Format the process name and basic info
        output = f"{prefix}{connector}{name_color}{Style.BRIGHT}{process.name}{Style.RESET_ALL} "
        output += f"{Fore.YELLOW}[PID: {process.pid}]{Style.RESET_ALL}"
        
        # Add resource information if requested
        if self.show_resources:
            cpu_color = Fore.RED if process.cpu_percent > 50 else Fore.GREEN if process.cpu_percent > 10 else Fore.WHITE
            mem_color = Fore.RED if process.memory_mb > 500 else Fore.YELLOW if process.memory_mb > 100 else Fore.WHITE
            
            output += f" {cpu_color}CPU: {process.cpu_percent:.1f}%{Style.RESET_ALL}"
            output += f" {mem_color}MEM: {process.format_memory()}{Style.RESET_ALL}"
        
        # Add verbose information
        if self.verbose:
            output += f" {Fore.MAGENTA}User: {process.username}{Style.RESET_ALL}"
            output += f" {Fore.BLUE}Threads: {process.num_threads}{Style.RESET_ALL}"
            output += f" {Fore.CYAN}Uptime: {process.format_uptime()}{Style.RESET_ALL}"
        
        print(output)
        
        # Display command line in verbose mode
        if self.verbose and process.cmdline != process.name:
            cmd_prefix = prefix + ("    " if is_last else "│   ")
            max_cmd_len = 80
            cmd_display = process.cmdline[:max_cmd_len]
            if len(process.cmdline) > max_cmd_len:
                cmd_display += "..."
            print(f"{cmd_prefix}{Fore.WHITE}{Style.DIM}└─ {cmd_display}{Style.RESET_ALL}")
        
        # Recursively display children
        for i, child in enumerate(process.children):
            is_last_child = (i == len(process.children) - 1)
            extension = "    " if is_last else "│   "
            self.display_tree(child, prefix + extension, is_last_child, visited)
    
    def find_process(self, query: str) -> List[ProcessInfo]:
        """
        Find processes matching the query (by name or PID).
        
        Args:
            query: Process name or PID to search for
            
        Returns:
            List of matching ProcessInfo objects
        """
        matches = []
        
        # Check if query is a PID
        try:
            pid = int(query)
            if pid in self.processes:
                matches.append(self.processes[pid])
            return matches
        except ValueError:
            pass
        
        # Search by name
        query_lower = query.lower()
        for proc in self.processes.values():
            if query_lower in proc.name.lower():
                matches.append(proc)
        
        return matches
    
    def display_process_subtree(self, pid: int) -> None:
        """Display only a specific process and its descendants"""
        if pid not in self.processes:
            print(f"{Fore.RED}Process with PID {pid} not found{Style.RESET_ALL}")
            return
        
        process = self.processes[pid]
        print(f"\n{Fore.CYAN}Process Subtree for: {Style.BRIGHT}{process.name}{Style.RESET_ALL}")
        print(f"{Fore.CYAN}{'═' * 70}{Style.RESET_ALL}\n")
        self.display_tree(process)
    
    def export_to_file(self, filename: str) -> None:
        """Export the process tree to a text file"""
        try:
            original_stdout = sys.stdout
            with open(filename, 'w', encoding='utf-8') as f:
                sys.stdout = f
                self.display_header()
                self.display_tree()
            sys.stdout = original_stdout
            print(f"{Fore.GREEN}Process tree exported to {filename}{Style.RESET_ALL}")
        except Exception as e:
            print(f"{Fore.RED}Error exporting to file: {e}{Style.RESET_ALL}")
    
    def display_header(self) -> None:
        """Display header information"""
        print(f"{Fore.CYAN}{Style.BRIGHT}{'═' * 70}{Style.RESET_ALL}")
        print(f"{Fore.CYAN}{Style.BRIGHT}Process Tree Visualizer{Style.RESET_ALL}")
        print(f"{Fore.CYAN}Created by: Michael Semera{Style.RESET_ALL}")
        print(f"{Fore.CYAN}Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}{Style.RESET_ALL}")
        print(f"{Fore.CYAN}Total Processes: {len(self.processes)}{Style.RESET_ALL}")
        print(f"{Fore.CYAN}{Style.BRIGHT}{'═' * 70}{Style.RESET_ALL}\n")
    
    def get_statistics(self) -> Dict:
        """Calculate and return process statistics"""
        stats = {
            'total_processes': len(self.processes),
            'root_processes': len(self.root_processes),
            'total_memory_mb': sum(p.memory_mb for p in self.processes.values()),
            'total_threads': sum(p.num_threads for p in self.processes.values()),
            'running': sum(1 for p in self.processes.values() if p.status == 'running'),
            'sleeping': sum(1 for p in self.processes.values() if p.status == 'sleeping'),
            'zombie': sum(1 for p in self.processes.values() if p.status == 'zombie')
        }
        return stats
    
    def display_statistics(self) -> None:
        """Display process statistics"""
        stats = self.get_statistics()
        print(f"\n{Fore.CYAN}{Style.BRIGHT}Process Statistics:{Style.RESET_ALL}")
        print(f"{Fore.WHITE}Total Processes: {Fore.GREEN}{stats['total_processes']}{Style.RESET_ALL}")
        print(f"{Fore.WHITE}Root Processes: {Fore.GREEN}{stats['root_processes']}{Style.RESET_ALL}")
        print(f"{Fore.WHITE}Total Memory: {Fore.YELLOW}{stats['total_memory_mb']:.1f} MB{Style.RESET_ALL}")
        print(f"{Fore.WHITE}Total Threads: {Fore.CYAN}{stats['total_threads']}{Style.RESET_ALL}")
        print(f"{Fore.WHITE}Running: {Fore.GREEN}{stats['running']}{Style.RESET_ALL} | "
              f"Sleeping: {Fore.CYAN}{stats['sleeping']}{Style.RESET_ALL} | "
              f"Zombie: {Fore.RED}{stats['zombie']}{Style.RESET_ALL}")


def main():
    """Main entry point for the process tree visualizer"""
    parser = argparse.ArgumentParser(
        description='Process Tree Visualizer - Display running processes in a hierarchical tree structure',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                          # Display full process tree
  %(prog)s --resources              # Show CPU and memory usage
  %(prog)s --verbose                # Show detailed information
  %(prog)s --search chrome          # Find processes by name
  %(prog)s --pid 1234               # Show specific process tree
  %(prog)s --output tree.txt        # Export to file
  %(prog)s --stats                  # Show process statistics

Created by: Michael Semera
        """
    )
    
    parser.add_argument('-r', '--resources', action='store_true',
                        help='Show CPU and memory usage')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show verbose process information')
    parser.add_argument('-p', '--pid', type=int,
                        help='Show only the specified process and its children')
    parser.add_argument('-s', '--search', type=str,
                        help='Search for processes by name')
    parser.add_argument('-o', '--output', type=str,
                        help='Export process tree to file')
    parser.add_argument('--stats', action='store_true',
                        help='Display process statistics')
    parser.add_argument('--monitor', action='store_true',
                        help='Continuous monitoring mode')
    parser.add_argument('--interval', type=float, default=2.0,
                        help='Update interval for monitoring mode (seconds)')
    
    args = parser.parse_args()
    
    def display_tree():
        """Display the process tree based on arguments"""
        tree = ProcessTree(show_resources=args.resources, verbose=args.verbose)
        tree.collect_processes()
        tree.build_tree()
        
        # Display header
        tree.display_header()
        
        # Handle different display modes
        if args.search:
            matches = tree.find_process(args.search)
            if matches:
                print(f"{Fore.GREEN}Found {len(matches)} matching process(es):{Style.RESET_ALL}\n")
                for proc in matches:
                    tree.display_process_subtree(proc.pid)
            else:
                print(f"{Fore.RED}No processes found matching '{args.search}'{Style.RESET_ALL}")
        elif args.pid:
            tree.display_process_subtree(args.pid)
        else:
            tree.display_tree()
        
        # Display statistics if requested
        if args.stats:
            tree.display_statistics()
        
        # Export to file if requested
        if args.output:
            tree.export_to_file(args.output)
    
    try:
        if args.monitor:
            print(f"{Fore.CYAN}Entering monitoring mode (Ctrl+C to exit)...{Style.RESET_ALL}\n")
            while True:
                # Clear screen (cross-platform)
                print("\033[2J\033[H", end="")
                display_tree()
                time.sleep(args.interval)
        else:
            display_tree()
    
    except KeyboardInterrupt:
        print(f"\n{Fore.YELLOW}Process tree visualizer terminated by user{Style.RESET_ALL}")
        sys.exit(0)
    except Exception as e:
        print(f"{Fore.RED}Unexpected error: {e}{Style.RESET_ALL}")
        sys.exit(1)


if __name__ == "__main__":
    main()