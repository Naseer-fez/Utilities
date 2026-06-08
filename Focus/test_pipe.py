import struct
import sys

# Define named pipe path
PIPE_PATH = r'\\.\pipe\FocusModeEnginePipe'

def run_test():
    print(f"Connecting to named pipe: {PIPE_PATH}...")
    try:
        # Open the named pipe on Windows as a binary file
        # We must use 'r+b' and buffering=0 for raw interactive read/write
        pipe = open(PIPE_PATH, 'r+b', buffering=0)
        print("Connected successfully!")
    except Exception as e:
        print(f"Connection failed: {e}")
        return

    # Build the message structure.
    # type: int (4 bytes) -> 0 for GetStatus
    # profileName: wchar_t[64] (128 bytes) -> zeros
    # durationMinutes: int (4 bytes) -> 0
    # unlockCode: wchar_t[16] (32 bytes) -> zeros
    # success: bool (1 byte) -> False (0)
    # statusText: wchar_t[256] (512 bytes) -> zeros
    # timeRemainingSeconds: int (4 bytes) -> 0
    # isStrictActive: bool (1 byte) -> False (0)
    
    # We can just construct a byte array of the exact size or larger.
    # What is the total size of IpcMessage in C++?
    # Let's count fields:
    # int type (4)
    # wchar_t profileName[64] (128)
    # int durationMinutes (4)
    # wchar_t unlockCode[16] (32)
    # bool success (1)
    # wchar_t statusText[256] (512)
    # int timeRemainingSeconds (4)
    # bool isStrictActive (1)
    #
    # In C++, sizeof(IpcMessage) is:
    # Let's write a quick python script to send 696 bytes (which is close to the sum: 4+128+4+32+1+512+4+1 = 686 plus alignment padding).
    # Actually, let's just make it 1024 bytes of 0s, and write the first 4 bytes as type 0 (GetStatus).
    # Since named pipe ReadFile in C++ expects sizeof(IpcMessage), if we send exactly the correct size, it will read it.
    # Let's see: what is the exact sizeof(IpcMessage) in C++?
    # We can calculate it:
    # type: 4 bytes (offset 0)
    # profileName: 128 bytes (offset 4)
    # durationMinutes: 4 bytes (offset 132)
    # unlockCode: 32 bytes (offset 136)
    # success: 1 byte (offset 168)
    # padding: 3 bytes (offset 169-171)
    # statusText: 512 bytes (offset 172)
    # timeRemainingSeconds: 4 bytes (offset 684)
    # isStrictActive: 1 byte (offset 688)
    # padding: 3 bytes (offset 689-691)
    # Total size = 692 bytes!
    # Let's verify by sending exactly 692 bytes.
    
    total_size = 692
    msg = bytearray(total_size)
    
    # Set command type to 0 (GetStatus)
    msg[0:4] = struct.pack('<i', 0)
    
    try:
        print("Sending GetStatus command...")
        pipe.write(msg)
        print("Reading response...")
        resp = pipe.read(total_size)
        print(f"Read {len(resp)} bytes response.")
        
        # Unpack the response
        resp_type = struct.unpack('<i', resp[0:4])[0]
        
        # Profile Name (offset 4, size 128)
        profile_bytes = resp[4:132]
        profile_name = profile_bytes.decode('utf-16').split('\x00')[0]
        
        duration = struct.unpack('<i', resp[132:136])[0]
        
        unlock_bytes = resp[136:168]
        unlock_code = unlock_bytes.decode('utf-16').split('\x00')[0]
        
        success_val = resp[168] != 0
        
        status_bytes = resp[172:684]
        status_text = status_bytes.decode('utf-16').split('\x00')[0]
        
        time_rem = struct.unpack('<i', resp[684:688])[0]
        strict_act = resp[688] != 0
        
        print("\n=== Focus Engine Status ===")
        print(f"Strict Active:  {strict_act}")
        print(f"Time Remaining: {time_rem} seconds")
        print(f"Active Profile: {profile_name}")
        print(f"Unlock Code:    {unlock_code}")
        print(f"Success Flag:   {success_val}")
        print(f"Status Text:    {status_text}")
        print("===========================\n")
        
    except Exception as e:
        print(f"IPC interaction failed: {e}")
    finally:
        pipe.close()

if __name__ == '__main__':
    run_test()
