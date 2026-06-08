import struct
import sys

# Define named pipe path
PIPE_PATH = r'\\.\pipe\FocusModeEnginePipe'

def run_test():
    print(f"Connecting to named pipe: {PIPE_PATH}...")
    try:
        pipe = open(PIPE_PATH, 'r+b', buffering=0)
        print("Connected successfully!")
    except Exception as e:
        print(f"Connection failed: {e}")
        return

    # Build StartSession message (size 692)
    # type: int (4 bytes) -> 1 for StartSession
    # profileName: wchar_t[64] (128 bytes) -> "Deep Work"
    # durationMinutes: int (4 bytes) -> 1
    # unlockCode: wchar_t[16] (32 bytes) -> zeros
    # success: bool (1 byte) -> False (0)
    # statusText: wchar_t[256] (512 bytes) -> zeros
    # timeRemainingSeconds: int (4 bytes) -> 0
    # isStrictActive: bool (1 byte) -> False (0)
    
    total_size = 692
    msg = bytearray(total_size)
    
    # Set type to 1 (StartSession)
    msg[0:4] = struct.pack('<i', 1)
    
    # Set profileName to "Deep Work" (offset 4, size 128)
    profile_name = "Deep Work"
    profile_encoded = profile_name.encode('utf-16le')
    msg[4:4+len(profile_encoded)] = profile_encoded
    
    # Set durationMinutes to 1 (offset 132, size 4)
    msg[132:136] = struct.pack('<i', 1)
    
    try:
        print("Sending StartSession command for 'Deep Work'...")
        pipe.write(msg)
        print("Reading response...")
        resp = pipe.read(total_size)
        print(f"Read {len(resp)} bytes response.")
        
        # Unpack the response
        resp_type = struct.unpack('<i', resp[0:4])[0]
        success_val = resp[168] != 0
        
        status_bytes = resp[172:684]
        status_text = status_bytes.decode('utf-16').split('\x00')[0]
        
        print("\n=== Start Session Reply ===")
        print(f"Success: {success_val}")
        print(f"Status:  {status_text}")
        print("===========================\n")
        
    except Exception as e:
        print(f"IPC interaction failed: {e}")
    finally:
        pipe.close()

if __name__ == '__main__':
    run_test()
