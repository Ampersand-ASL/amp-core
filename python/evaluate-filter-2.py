# 8K<->48K filter
import numpy as np
from scipy.signal import firwin, lfilter, lfilter_zi
import matplotlib.pyplot as plt
import math

# https://www.sjsu.edu/people/burford.furman/docs/me120/FFT_tutorial_NI.pdf

def fir_freq_response(coefficients, sampling_rate):
    # Calculate the frequency response using the DFT (FFT)
    frequency_response = np.fft.fft(coefficients)    
    # Number of coefficients
    num_coeffs = len(coefficients)
    # Generate the corresponding frequency values
    frequencies = np.fft.fftfreq(num_coeffs, 1 / sampling_rate)
    return frequencies, frequency_response

sample_hz = 48000
fft_n = 1024
resolution_hz = sample_hz / fft_n

# Ampersand
h1 = np.array([ 
    -84, -49, 5, 64, 114, 139, 129, 82, 7, -80, -155, -197, -190, -130, -28, 95, 207, 275, 277, 203, 65, -109, -274, -386, -407, -318, -130, 120, 374, 564, 627, 526, 260, -129, -560, -926, -1113, -1026, -610, 134, 1143, 2298, 3445, 4417, 5068, 5297, 5068, 4417, 3445, 2298, 1143, 134, -610, -1026, -1113, -926, -560, -129, 260, 526, 627, 564, 374, 120, -130, -318, -407, -386, -274, -109, 65, 203, 277, 275, 207, 95, -28, -130, -190, -197, -155, -80, 7, 82, 129, 139, 114, 64, 5, -49, -84
])
# Calculate the frequency response
frequencies1, frequency_response1 = fir_freq_response(h1 / 32767, sample_hz)
db_data1 = 20 * np.log10(np.abs(frequency_response1[:len(frequencies1)//2]))

# app_rpt
h2 = np.array([ 
    103, 136, 148, 74, -113, -395, -694, -881, -801, -331, 573, 1836, 3265, 4589, 5525, 5864, 5525,
    4589, 3265, 1836, 573, -331, -801, -881, -694, -395, -113,
    74, 148, 136, 103 
]) 
# Calculate the frequency response
frequencies2, frequency_response2 = fir_freq_response(h2 / 32767, sample_hz)
db_data2 = 20 * np.log10(np.abs(frequency_response2[:len(frequencies2)//2]))

# Make a test signal
amp_0 = 1
freq_0 = 10 * resolution_hz
duration = 0.02 * 2
num_points = int(duration * sample_hz)

# Create a time array using linspace
t = np.linspace(0.0, duration, num_points, endpoint=False)
wave_0 = amp_0 * np.sin(2 * np.pi * freq_0 * t)

# Magnitude (given)
print("Mag of signal (Vpeak)", amp_0)
# RMS (definition of RMS)
wave_0_rms =  np.sqrt(np.mean(np.square(wave_0)))
print("Vrms of signal ", wave_0_rms)
print("Vrms of signal (mag / root(2))", amp_0 / np.sqrt(2))
# Power 
wave_0_power = wave_0_rms * wave_0_rms
print("Power of signal ", wave_0_power)

print()

# Calculate the frequency response using the DFT (FFT)
resp_0 = np.fft.fft(wave_0[0:1024])
abs_val = np.abs(resp_0) / fft_n
max_bin = np.argmax(abs_val)

left_mag = abs_val[max_bin]
print("Mag of left", left_mag)
left_power = (left_mag * left_mag) 
print("Power of left", left_power)

right_mag = abs_val[fft_n - max_bin]
print("Mag of right", right_mag)
right_power = right_mag * right_mag
print("Power of right", right_power)

both_power = left_power + right_power
print("Power of left + right", both_power)

print()

# Compute power of everything else
other_power = 0
total_power = 0
for i in range(0, int(fft_n)):
    total_power += abs_val[i] * abs_val[i]
    # Skip the two bins that we've already looked at
    if i != max_bin and i != fft_n - max_bin:
        other_power += abs_val[i] * abs_val[i]

print("Power of total " , total_power)
print("Power of other " , other_power)
print("Power of left + right + other", both_power + other_power)
print("Fraction other/total dB", 10.0 * math.log10(other_power/total_power))

"""
# Plot the magnitude response
fig, ax = plt.subplots()
ax.plot(frequencies1[:len(frequencies1)//2], db_data1)
ax.plot(frequencies2[:len(frequencies2)//2], db_data2)
ax.set_xscale('log') 
ax.set_title('ASL 48k<->8k Decimation/Interpolation LPF Filters')
ax.set_xlabel('Frequency (Hz)')
ax.set_ylabel('Magnitude (dB)')
ax.axhline(-3, color='grey', linestyle=':', label='-3dB')
ax.axvline(4600, color='red', linestyle='--', label='4.6k')
ax.axvline(4000, color='blue', linestyle='--', label='4k')
ax.axvspan(4000, 24000, color=(0.9, 0.9, 1.0), label='Potential Aliasing Area (up to 24k)')
ax.legend()
ax.set_ylim(-65, 1) 
ax.grid(True)

plt.show()
"""