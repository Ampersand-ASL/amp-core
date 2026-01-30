# 8K<->48K filter
import numpy as np
from scipy.signal import firwin, lfilter, lfilter_zi
import matplotlib.pyplot as plt

def fir_freq_response(coefficients, sampling_rate):
    # Calculate the frequency response using the DFT (FFT)
    frequency_response = np.fft.fft(coefficients)    
    # Number of coefficients
    num_coeffs = len(coefficients)
    # Generate the corresponding frequency values
    frequencies = np.fft.fftfreq(num_coeffs, 1 / sampling_rate)
    return frequencies, frequency_response

sample_rate = 48000
nyq_rate = sample_rate / 2.0
# The cutoff frequency of the filter.
cutoff_hz = 4300
# The desired width of the transition from pass to stop,
# relative to the Nyquist rate. 
#width = (6300 - 2900) / nyq_rate
# The desired ripple in dB
#ripple_db = 0.5

# Create a FIR filter using a Kaiser window methodology
N1 = 91
# This beta factor trades off steepness of roll-off
# with the depth of the stop-band attenuation. 
# A lower number means steeper roll-off and less
# stop-band (final) attenuation.
# See this standard: 
# https://www.etsi.org/deliver/etsi_es/202700_202799/202737/01.08.01_60/es_202737v010801p.pdf
beta1 = 1
cutoff_hz1 = 3900
# Use firwin with a Kaiser window to create a lowpass FIR filter.
taps1 = firwin(N1, cutoff_hz1 / nyq_rate, window=('kaiser', beta1))
# Calculate the frequency response
frequencies1, frequency_response1 = fir_freq_response(taps1, sample_rate)
db_data1 = 20 * np.log10(np.abs(frequency_response1[:len(frequencies1)//2]))
print(taps1)

# The FIR filter from the ASL code
N2 = 31
taps2 = [ 103, 136, 148, 74, -113, -395, -694,
        -881, -801, -331, 573, 1836, 3265, 4589, 5525, 5864, 5525,
        4589, 3265, 1836, 573, -331, -801, -881, -694, -395, -113,
        74, 148, 136, 103 ]
taps2 = [ t / 32767 for t in taps2]
# Calculate the frequency response
frequencies2, frequency_response2 = fir_freq_response(taps2, sample_rate)
db_data2 = 20 * np.log10(np.abs(frequency_response2[:len(frequencies2)//2]))

# Plot the magnitude response
fig, ax = plt.subplots()
ax.plot(frequencies2[:len(frequencies2)//2], db_data2)
ax.plot(frequencies1[:len(frequencies1)//2], db_data1)
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
fig, ax = plt.subplots()
ax.set_title('LPF Filter Impulse Response')
ax.plot(taps, label="fc=4.3kHz, Kaiser beta=3.0")
ax.plot(taps2,label="chan_simpleusb.c")
ax.grid(True)
ax.legend()
plt.show()

# Sanity check: generate a sample signal of one second
t = np.linspace(0, sample_rate, sample_rate, endpoint=False)
# One tone is below a one tone is above the cutoff
ft = 2000
ft2 = 6000
omega = 2 * np.pi * ft / sample_rate
omega2 = 2 * np.pi * ft2 / sample_rate
signal = np.cos(omega * t) + np.cos(omega2 * t)

# Apply the FIR filter in blocks to simulate a network streaming
filtered_signal = []
# The size of IAX2 network blocks
block_size = 160
blocks = int(len(signal) / block_size)

# The zi object is used to maintain the state inside of the
# FIR since we are applying data one block at a time.
zi = lfilter_zi(taps, [1])

for block in range(0, blocks):
    # NOTE: zi is being passed around each time
    filtered_block, zi = lfilter(taps, [1.0], 
        signal[block * block_size:(block + 1) * block_size], zi=zi)
    filtered_signal.extend(filtered_block / 32767.0)

# Plot resulting signal
fig, ax = plt.subplots()
ax.plot(t[0:1024], filtered_signal[0:1024])
ax.set_title('Filtered Signal (ft=' + str(ft) + ')')
ax.set_xlabel('Time')
ax.set_ylabel('Magnitude')
ax.grid(True)
plt.show()
"""