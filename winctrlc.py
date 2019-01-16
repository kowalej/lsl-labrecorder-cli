import socket
import subprocess
import time
import random
import signal, os, sys


class Popen:
  _port = 8080
#  _connection = ''
  #_port = random.randint(10000, 50000)
  _connection = ''
  print(_port)


  def _start_ctrl_c_wrapper(self, cmd):
    cmd_str = "start /min \"\" python winctrlc.py "+"\""+cmd+"\""+" "+str(self._port)
    #cmd_str = "start \"\" python winctrlc.py "+"\""+cmd+"\""+" "+str(self._port)
    subprocess.Popen(cmd_str, shell=True)

  def _create_connection(self):
    self._connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    self._connection.connect(('localhost', self._port))
    #self._connection.connect(('127.0.0.1', self._port))


  def send_ctrl_c(self):
    self._connection.send(Wrapper.TERMINATION_REQ)
    self._connection.close()

  def __init__(self, cmd):
    self._start_ctrl_c_wrapper(cmd)
    self._create_connection()


class Wrapper:
  TERMINATION_REQ = "Terminate with CTRL-C"

  def _create_connection(self, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
#    s.bind(('localhost', port))
    s.bind(('127.0.0.1', port))
    s.listen(1)
    conn, addr = s.accept()
    return conn

  def _wait_on_ctrl_c_request(self, conn):
    while True:
      data = conn.recv(1024)
      if data == self.TERMINATION_REQ:
        ctrl_c_received = True
        break
      else:
        ctrl_c_received = False
    return ctrl_c_received

  def _cleanup_and_fire_ctrl_c(self, conn):
    conn.close()
    os.kill(signal.CTRL_C_EVENT, 0)

  def _signal_handler(self, signal, frame):
    time.sleep(1)
    sys.exit(0)

  def __init__(self, cmd, port):
    signal.signal(signal.SIGINT, self._signal_handler)
    subprocess.Popen(cmd)
    conn = self._create_connection(port)
    ctrl_c_req_received = self._wait_on_ctrl_c_request(conn)
    if ctrl_c_req_received:
      self._cleanup_and_fire_ctrl_c(conn)
    else:
      sys.exit(0)


if __name__ == "__main__":
  command_string = sys.argv[1]
  port_no = int(sys.argv[2])
  Wrapper(command_string, port_no)