import os
import json
import winctrlc
import sys 
import time
from subprocess import Popen, PIPE, signal
import win32api
import win32con

curia_xdf_recorder_path = "D://Documents//Curia//CuriaRecorder//labstreaminglayer//build//Apps//CuriaRecorder//Release//curiarecorder.exe"


pid = Popen([curia_xdf_recorder_path, 'record', "*", 'test.xdf', '-r', '-o'], stdin=PIPE) # call subprocess

v = input()
print("sending ctrl c")

try:
    pid.send_signal(signal.CTRL_C_EVENT)
    pid.wait(timeout=10000)
except KeyboardInterrupt:
	v = input('Press any key to finish')

#pid.wait(4000) 

#time.sleep(1)
#pid.send_signal(signal.CTRL_C_EVENT)

#pid.exit()
