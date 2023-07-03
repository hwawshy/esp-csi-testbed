import base64
import collections
import json
import math
import socket
import struct
import time
import matplotlib.pyplot as plt
import numpy as np

from wait_timer import WaitTimer

csi_size = 128
host_ip = "192.168.4.11"
host_port = 4951

amplitudes = {}
phases = {}

total_nodes = 2
cols = 2
node_plots = {}

# Variables to store CSI statistics
packet_count = 0
total_packet_counts = 0

# Wait Timers. Change these values to increase or decrease the rate of `print_stats` and `render_plot`.
print_stats_wait_timer = WaitTimer(1.0)
render_plot_wait_timer = WaitTimer(0.4)

# Create figure for plotting
plt.ion()
fig = plt.figure()
fig.canvas.draw()
plt.show(block=False)


def plot() -> None:
    # plt.clf()
    for node, ax in node_plots.items():
        ax.clear()
        amp = amplitudes[node]

        df = np.fft.fftshift(np.transpose(np.asarray(amp, dtype=np.int32)[:, 2:]))
        x = np.arange(100 - len(amp), 100, 1)
        y = np.arange(0, 62, 1)

        x, y = np.meshgrid(x, y)
        ax.plot_surface(x, y, df, rstride=1, cstride=1, cmap='viridis', edgecolor='none')

        ax.set_xlabel('Time')
        ax.set_ylabel('Subcarrier')
        ax.set_zlabel('Amplitude')

        ax.set(xlim=(0, 100))
        ax.set_title(node)

    fig.canvas.flush_events()
    plt.show(block=False)


def process(data: dict) -> None:
    csi = data['csi_raw']
    node = data['dnode']

    if node not in amplitudes:
        amplitudes[node] = collections.deque(maxlen=100)

    if node not in phases:
        phases[node] = collections.deque(maxlen=100)

    rows = math.ceil(total_nodes / cols)
    len_nodes = len(node_plots.keys())
    if node not in node_plots:
        ax = fig.add_subplot(rows, cols, len_nodes + 1, projection='3d')
        ax.view_init(azim=50)
        node_plots[node] = ax

    packet_amplitudes = []
    packet_phases = []
    for i in range(0, csi_size, 2):
        packet_amplitudes.append(math.sqrt(csi[i] ** 2 + csi[i + 1] ** 2))
        packet_phases.append(math.atan2(csi[i], csi[i + 1]))

    amplitudes[node].append(packet_amplitudes)
    phases[node].append(packet_phases)


sock_fd = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM, proto=0)
sock_fd.bind((host_ip, host_port))

while True:
    data, _ = sock_fd.recvfrom(1024)
    data = json.loads(data)
    data['real_timestamp'] = int(time.time())
    if 'csi_raw' in data:
        data['csi_raw'] = list(struct.unpack('b' * csi_size, base64.b64decode(data['csi_raw'], validate=True)))
    process(data)
    packet_count += 1
    total_packet_counts += 1

    if print_stats_wait_timer.check():
        print_stats_wait_timer.update()
        print("Packet Count:", packet_count, "per second.", "Total Count:", total_packet_counts)
        packet_count = 0

    if render_plot_wait_timer.check():
        render_plot_wait_timer.update()
        plot()
