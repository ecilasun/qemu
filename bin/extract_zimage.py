import sys
import struct

def find_zimage(filepath, outpath):
    with open(filepath, 'rb') as f:
        data = f.read()

    # zImage magic is 0x016f2818 at offset 0x24
    # So we look for the sequence, but we need to find the START of the zImage.
    # The magic is at start + 0x24.
    
    # We search for the magic number
    magic = b'\x18\x28\x6f\x01' # Little endian 0x016f2818
    
    offset = 0
    while True:
        idx = data.find(magic, offset)
        if idx == -1:
            print("zImage magic not found.")
            return False
        
        # Check if this is a valid start
        start = idx - 0x24
        if start < 0:
            offset = idx + 1
            continue
            
        # Verify start instruction (usually branch)
        # ARM zImage starts with 0xEA0000A0 or similar branch
        # But let's just assume if magic matches, it's likely it.
        
        print(f"Found zImage magic at offset {idx} (0x{idx:x})")
        print(f"Potential zImage start at {start} (0x{start:x})")
        
        # In a FIT image, the data is stored in a 'data' property.
        # The length is stored in the DTB structure, but extracting it properly requires parsing DTB.
        # However, usually the zImage is contiguous.
        # We can try to extract from 'start' to the end, or try to find the size.
        # zImage has a start and end address in the header?
        # Offset 0x28 and 0x2C contain start/end? No.
        
        # Let's look at the zImage header.
        # 0x24: Magic
        # 0x28: Start address
        # 0x2C: End address
        
        # Actually, zImage is self-extracting.
        # We can just extract from 'start' to the end of the file?
        # Or we can try to parse the FIT structure.
        
        # Let's try to extract everything from start and see if it works.
        # But wait, FIT image might have other things after it (ramdisk).
        # We need the size.
        
        # In FIT image (DTB), the data is a byte array.
        # The DTB structure:
        # token (4 bytes)
        # size (4 bytes)
        # data (size bytes)
        
        # If we find the data start, the 4 bytes BEFORE it might be the size?
        # In DTB, property value is: len (4 bytes), nameoff (4 bytes), data (len bytes).
        # So 8 bytes before the data start should be the length?
        # Let's check 8 bytes before 'start'.
        
        try:
            # DTB is big endian
            len_bytes = data[start-8:start-4]
            length = struct.unpack('>I', len_bytes)[0]
            print(f"Size from DTB property header: {length} bytes")
            
            # Sanity check size
            if length > 0 and length < len(data):
                # Extract
                zimage_data = data[start:start+length]
                with open(outpath, 'wb') as out:
                    out.write(zimage_data)
                print(f"Extracted {length} bytes to {outpath}")
                return True
        except:
            pass
            
        offset = idx + 1

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: extract_zimage.py <image.ub> <out_zimage>")
        sys.exit(1)
        
    find_zimage(sys.argv[1], sys.argv[2])
