# Enhanced Network Ping Tool

A feature-rich network ping utility with GTK-based graphical interface, supporting multiple targets and advanced configuration options.

## Features

### Core Functionality
- Multiple simultaneous ping targets
- Configurable packet sizes (32-65527 bytes)
- Adjustable ping intervals (100-5000ms)
- Customizable timeout settings
- Continuous or single-shot ping modes

### Display Options
- Real-time ping results
- Timestamp display
- DNS resolution
- TTL (Time To Live) values
- Dark theme interface
- Auto-scrolling output

### Statistics
- Packets sent/received
- Round-trip time (RTT)
- Packet loss calculation
- Minimum/Maximum/Average RTT
- Jitter calculations

## Requirements

### Linux
- GTK+ 3.0 or later
- GCC compiler
- Root privileges (for raw sockets)
- pthread library
- Required development packages:
  ```bash
  # Ubuntu/Debian
  sudo apt-get install build-essential libgtk-3-dev

  # Fedora
  sudo dnf install gcc gtk3-devel

  # Arch Linux
  sudo pacman -S gcc gtk3
  ```

## Building

1. Clone the repository:
```bash
git clone https://github.com/Okalmary254/enhanced-ping.git
cd enhanced-ping
```

2. Compile the program:
```bash
gcc fping.c -o fping -lm `pkg-config --cflags --libs gtk+-3.0` -pthread
```

## Usage

1. Launch the program with root privileges:
```bash
sudo ./fping
```

2. Using the Interface:
   - Enter hostname or IP in the input field
   - Click "Add Host" or press Enter to add target
   - Configure ping options in the settings panel
   - Click "Start Ping" to begin
   - Use "Stop Ping" to halt operations
   - "Clear Results" removes all output

### Configuration Options

- **Packet Size**: Set the size of ping packets (32-65527 bytes)
- **Interval**: Time between pings (100-5000ms)
- **Timeout**: Maximum wait time for responses
- **Continuous Ping**: Toggle between continuous and single-shot modes
- **Show Timestamp**: Display time for each ping
- **Resolve DNS**: Show hostnames instead of IP addresses

## Troubleshooting

### Common Issues

1. "Operation not permitted":
   - Run the program with sudo
   - Check user permissions

2. "Address already in use":
   - Wait a few moments and try again
   - Check for other ping processes

3. GUI not appearing:
   - Verify GTK3 installation
   - Check X server connection

4. High CPU usage:
   - Increase ping interval
   - Reduce number of targets
   - Disable continuous mode

### Debug Tips

1. Enable verbose output:
   - Check "Show Timestamp" for detailed timing
   - Monitor system logs for socket errors

2. Performance optimization:
   - Use appropriate intervals (>500ms recommended)
   - Limit simultaneous targets (<5 recommended)
   - Clear results periodically for long sessions

## Contributing

1. Fork the repository
2. Create your feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- GTK+ Team for the GUI framework
- Linux networking stack developers
- Community contributors and testers
