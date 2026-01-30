"""
Generation of FIR LPF for interpolation from 16K to 48K

Copyright (C) Bruce MacKinnon, 2025
Software Defined Repeater Controller Project

Need to add this to the path:
export PYTHONPATH=../../firpm-py
"""
import numpy as np
import matplotlib.pyplot as plt
import math
#import firpm 
from scipy.signal import lfilter, firwin

def fir_freq_response(coefficients, sampling_rate):
    # Calculate the frequency response using the DFT (FFT)
    frequency_response = np.fft.fft(coefficients)    
    # Number of coefficients
    num_coeffs = len(coefficients)
    # Generate the corresponding frequency values
    frequencies = np.fft.fftfreq(num_coeffs, 1/sampling_rate)
    return frequencies, frequency_response

fs = 48000
sample_rate = fs
nyq_rate = sample_rate / 2.0

# Create a FIR filter using a Kaiser window methodology
N1 = 71
# This beta factor trades off steepness of roll-off
# with the depth of the stop-band attenuation. 
# A lower number means steeper roll-off and less
# stop-band (final) attenuation.
# See this standard: 
# https://www.etsi.org/deliver/etsi_es/202700_202799/202737/01.08.01_60/es_202737v010801p.pdf
beta1 = 1
cutoff_hz1 = 7700
# Use firwin with a Kaiser window to create a lowpass FIR filter.
taps1 = firwin(N1, cutoff_hz1 / nyq_rate, window=('kaiser', beta1))
print([round(t * 32767) for t in taps1])

# Calculate the frequency response
frequencies1, frequency_response1 = fir_freq_response(taps1, sample_rate)
db_data1 = 20 * np.log10(np.abs(frequency_response1[:len(frequencies1)//2]))

# Calculate the frequency response
frequencies1, frequency_response1 = fir_freq_response(taps1, fs)

# Plot the magnitude response
db_data = 20 * np.log10(np.abs(frequency_response1[:len(frequencies1)//2]))
fig, ax = plt.subplots()
ax.plot(frequencies1[:len(frequencies1)//2], db_data)
ax.set_xscale('log') 
ax.set_title('ASL HD (16K) Decimation/Interpolation LPF Filter')
ax.set_xlabel('Frequency (Hz)')
ax.set_ylabel('Magnitude (dB)')
ax.axhline(-3, color='grey', linestyle=':', label='-3dB')
ax.axvline(8000, color='blue', linestyle='--', label='8k')
ax.axvline(9000, color='red', linestyle='--', label='9k')
ax.axvline(4000, color=(0.8, 0.8, 0.8), linestyle=':', label='4k')
ax.axvspan(8000, 24000, color=(0.9, 0.9, 1.0), label='Potential Aliasing Area')
ax.legend()
ax.grid(True)
ax.set_ylim(-65, 1) 
plt.show()
"""
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
"""
