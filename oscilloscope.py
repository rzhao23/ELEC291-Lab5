#!/usr/bin/env python3

import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import serial
import serial.tools.list_ports

# Configuration

BAUD        = 115200
N_POINTS    = 2000       # sample points per screen redraw
N_PERIODS   = 2.5        # how many full periods to show on screen (mutable)
UPDATE_MS   = 150        # animation refresh interval in milliseconds

# Square wave generator

def square_wave(t_s, freq_hz, amplitude, phase_deg):
    """
    Return a unipolar square wave sampled at times t_s.
    High level = amplitude, low level = 0.
    phase_deg shifts the waveform left (positive = leads CH1).
    """
    phase_rad = np.radians(phase_deg)
    return amplitude * (np.sin(2 * np.pi * freq_hz * t_s + phase_rad) >= 0).astype(float)

# Serial helpers

def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
    else:
        print("Available serial ports:")
        for p in ports:
            print(f"  {p.device:<12}  {p.description}")

def open_serial(port):
    try:
        ser = serial.Serial(port, BAUD, timeout=0)  # non-blocking
        print(f"Connected to {port} at {BAUD} baud")
        return ser
    except serial.SerialException as e:
        print(f"Cannot open {port}: {e}")
        list_ports()
        sys.exit(1)

def parse_packet(line: str):
    """
    Parse a '$v1,v2,freq,phase' line.
    Returns a dict on success, None otherwise.
    """
    line = line.strip()
    if not line.startswith('$'):
        return None
    try:
        parts = line[1:].split(',')
        return {
            'v1':    float(parts[0]),
            'v2':    float(parts[1]),
            'freq':  max(int(parts[2]), 1),   # guard against 0 Hz
            'phase': float(parts[3]),
        }
    except (ValueError, IndexError):
        return None

# Entry point / port selection

if len(sys.argv) < 2:
    print("Usage: python oscilloscope.py <PORT>")
    print()
    list_ports()
    sys.exit(0)

PORT = sys.argv[1]
ser  = open_serial(PORT)

# Shared state updated by the animation callback

state = {'v1': 1.0, 'v2': 1.0, 'freq': 60, 'phase': 0.0}
rx_buf = ''

# Matplotlib figure

BG      = '#111118'
PANEL   = '#1a1a28'
GRID    = '#252535'
GREEN   = '#ffd700'
ORANGE  = '#4488ff'
TEXT    = '#ccccdd'

fig, ax = plt.subplots(figsize=(11, 5))
fig.patch.set_facecolor(BG)
ax.set_facecolor(PANEL)
for spine in ax.spines.values():
    spine.set_color(GRID)
ax.tick_params(colors=TEXT, labelsize=9)
ax.grid(True, color=GRID, linewidth=0.6, linestyle='--')
ax.set_xlabel('Time  (ms)', color=TEXT, fontsize=10)
ax.set_ylabel('Voltage  (V)', color=TEXT, fontsize=10)
ax.set_title('ELEC291 Lab5  –  Oscilloscope', color='#eeeeff', fontsize=12, pad=10)

line_ch1, = ax.plot([], [], color=GREEN,  linewidth=1.8, label='CH1  Vr  (P2.2 via P2.1 ADC)')  # yellow
line_ch2, = ax.plot([], [], color=ORANGE, linewidth=1.8, label='CH2  Vm  (P2.3 via P1.6 ADC)')  # blue

ax.legend(loc='upper right', facecolor=PANEL, edgecolor=GRID,
          labelcolor='white', fontsize=9, framealpha=0.85)

info = ax.text(
    0.01, 0.97, '',
    transform=ax.transAxes, verticalalignment='top',
    color='white', fontsize=9, fontfamily='monospace',
    bbox=dict(facecolor='#00000099', edgecolor='none', boxstyle='round,pad=0.4'),
)

# Animation callback

def update(_frame):
    global rx_buf, state, N_PERIODS

    # --- drain serial port ---
    try:
        chunk = ser.read(ser.in_waiting or 1)
        rx_buf += chunk.decode('ascii', errors='ignore')
    except serial.SerialException:
        return line_ch1, line_ch2, info

    # --- parse complete lines ---
    while '\n' in rx_buf:
        raw_line, rx_buf = rx_buf.split('\n', 1)
        parsed = parse_packet(raw_line)
        if parsed:
            state = parsed
        elif raw_line.strip() == '!psize:b':
            N_PERIODS = min(N_PERIODS + 0.5, 20.0)
        elif raw_line.strip() == '!psize:s':
            N_PERIODS = max(N_PERIODS - 0.5, 0.5)
        elif raw_line.strip() == '!ch1:on':
            line_ch1.set_visible(True)
        elif raw_line.strip() == '!ch1:off':
            line_ch1.set_visible(False)
        elif raw_line.strip() == '!ch2:on':
            line_ch2.set_visible(True)
        elif raw_line.strip() == '!ch2:off':
            line_ch2.set_visible(False)

    v1    = state['v1']
    v2    = state['v2']
    freq  = state['freq']
    phase = state['phase']

    # --- build time axis (N_PERIODS full periods) ---
    period_ms = 1000.0 / freq
    t_ms = np.linspace(0.0, N_PERIODS * period_ms, N_POINTS)
    t_s  = t_ms / 1000.0

    # --- generate square waves ---
    w1 = square_wave(t_s, freq, v1, 0.0)    # CH1: reference, no phase offset
    w2 = square_wave(t_s, freq, v2, phase)   # CH2: shifted by measured phase

    line_ch1.set_data(t_ms, w1)
    line_ch2.set_data(t_ms, w2)

    y_max = max(v1, v2, 0.5) * 1.35
    ax.set_xlim(0.0, N_PERIODS * period_ms)
    ax.set_ylim(-0.1, y_max)

    lead_lag = 'leads' if phase > 0 else ('lags' if phase < 0 else 'in phase')
    info.set_text(
        f"CH1  Vr  = {v1:.3f} V\n"
        f"CH2  Vm  = {v2:.3f} V\n"
        f"Freq     = {freq} Hz\n"
        f"Phase    = {phase:+.2f}°  (CH2 {lead_lag} CH1)"
    )

    return line_ch1, line_ch2, info


ani = animation.FuncAnimation(
    fig, update,
    interval=UPDATE_MS,
    blit=True,
    cache_frame_data=False,
)

plt.tight_layout()

try:
    plt.show()
finally:
    ser.close()
    print("Serial port closed.")
