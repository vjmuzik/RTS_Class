# Application 4 (Quest 1B) FreeRTOS Synchronization Quest (Part B): Signals, Sharing & Safety on the ESP32

![GIF of the ESP32 and components on it's breadboard](extras/ESP32Breadboard.gif)

## Analysis/Engineering

## Q1 Signal Discipline:
The binary semaphore acts as a "latched" signal, unblocking the event handler as soon as a press is detected. If pressed multiple times quickly, the binary semaphore only records a single "event" (staying at 1), causing subsequent pulses to be lost. A counting semaphore would instead queue every pulse up to its maximum, ensuring no press is missed.

## Q2 Event Flood Handling:
In my code it's not actually possible to do this, but if it was, rapidly crossing the threshold would fill the counting semaphore, causing the red LED to blink in a sustained sequence as the handler processes each stored increment. The counting semaphore ensures every threshold breach is handled in order. Swapping to a binary semaphore would cause "event loss," where 10 rapid breaches would only trigger a single LED blink.

## Q3 Protecting Shared Output: 
Removing the mutex causes "interleaved" strings, where a sensor print might cut into the middle of a button print. Mutexes ensure only a single thread accesses the hardware at a time. In the real world, interleaved logs could lead to misinterpreted medical data or corrupted shared variables (like a patient's recorded medication dose).

## Q4 Scheduling and Preemption:
Yes, the emergency_button_task (priority 3) immediately preempts the heart_rate_monitor_task (priority 2) the moment the GPIO goes low. During busy periods, the patient_heartbeat_task (priority 1) might experience "jitter" or slight delays in blinking because the higher-priority handler is hogging the CPU to process the alarm.

## Q5 Timing and Responsiveness: 
vTaskDelay provides a relative pause, meaning the actual polling period is 10ms+execution time, whereas vTaskDelayUntil ensures a constant frequency regardless of execution time. Our 10ms polling rate limits event detection; a pulse shorter than 10ms might be missed. I would switch to vTaskDelayUntil for the heart_rate_monitor_task to ensure medical vitals are sampled at a strictly consistent interval for medical accuracy.

## Q6 Theme Integration: 
The Green LED represents active communication with the machine reading the patient’s pulse; the Red LED is the Nurse Station alarm; the Counting Semaphore is the queue of abnormal vitals; and the Mutex is the "Patient Record" that only one nurse can write to at once. Synchronization is life-critical because a lost "Emergency Call" signal (button press) could result in a failure to provide life-saving intervention.

## Q7 [Bonus] Induced Failure: 
Yes I can, basically I upped the number of times the sensor is read and printed everytime it's read so it hogs the hardware for quite a bit of time, especially while the knob is moving.

![Mission Debrief Slides Voiceover](MissionDebrief.mov)
