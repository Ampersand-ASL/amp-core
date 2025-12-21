"""
Generation of FIR LPF for interpolation from 16K to 48K

Copyright (C) Bruce MacKinnon, 2025
Software Defined Repeater Controller Project

Need to add this to the path:
export PYTHONPATH=../../../firpm-py
"""
import numpy as np
import matplotlib.pyplot as plt
import math
import firpm 
from scipy.signal import lfilter, firwin

def fir_freq_response(coefficients, sampling_rate):
    # Calculate the frequency response using the DFT (FFT)
    frequency_response = np.fft.fft(coefficients)    
    # Number of coefficients
    num_coeffs = len(coefficients)
    # Generate the corresponding frequency values
    frequencies = np.fft.fftfreq(num_coeffs, 1/sampling_rate)
    return frequencies, frequency_response

taps = 45
fs = 48000
wc0 = 7000 / fs
wc1 = 8000 / fs
h, _ = firpm.design(taps, 1, 2, [ 0.00, wc0, wc1, 0.5 ], [ 1.0, 0.0 ], [ 1.0, 1.0 ] )
h_reverse = list(h)
h_reverse.reverse()

h_reverse_fixed = []
for j in h_reverse:
    h_reverse_fixed.append(int(j * 32767.0))

print("Reverse Impulse Response", h_reverse_fixed)

sampling_rate = 1  # Hz

# Calculate the frequency response
frequencies, frequency_response = fir_freq_response(h, fs)

# Plot the magnitude response
db_data = 20 * np.log10(np.abs(frequency_response[:len(frequencies)//2]))
fig, ax = plt.subplots()
ax.plot(frequencies[:len(frequencies)//2], db_data)
ax.set_xscale('log') 
ax.set_title('ASL HD (16K) Decimation/Interpolation LPF Filter (N=' + str(taps) + ')')
ax.set_xlabel('Frequency (Hz)')
ax.set_ylabel('Magnitude (dB)')
ax.axvline(8000, color='red', linestyle='--', label='8k')
ax.axvline(7000, color='green', linestyle='--', label='7k')
ax.axvline(4000, color=(0.8, 0.8, 0.8), linestyle=':', label='4k')
ax.axvspan(8000, 24000, color=(0.9, 0.9, 1.0), label='Potential Aliasing Area')
ax.legend()
ax.grid(True)
plt.show()

# Sanity check: generate a sample signal of one second
t = np.linspace(0, fs, fs, endpoint=False)
ft = 12000
omega = 2 * 3.1415926 * ft / fs
signal = np.sin(omega * t)

# Apply the FIR filter
filtered_signal = lfilter(h, [1.0], signal)

fig, ax = plt.subplots()
ax.plot(t[41:1000], filtered_signal[41:1000])
ax.set_title('Filtered Signal (ft=' + str(ft) + ')')
ax.set_xlabel('Tone Freq (Hz)')
ax.set_ylabel('Magnitude')
ax.grid(True)
plt.show()
