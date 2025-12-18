import sys
import struct

def extract_components(filepath, out_zimage, out_dtb):
    with open(filepath, 'rb') as f:
        data = f.read()

    # --- Extract zImage ---
    # zImage magic is 0x016f2818 at offset 0x24
    zimage_magic = b'\x18\x28\x6f\x01' # Little endian 0x016f2818
    
    offset = 0
    zimage_found = False
    while True:
        idx = data.find(zimage_magic, offset)
        if idx == -1:
            print("zImage magic not found.")
            break
        
        start = idx - 0x24
        if start < 0:
            offset = idx + 1
            continue
            
        try:
            # Check for DTB property length (8 bytes before data)
            len_bytes = data[start-8:start-4]
            length = struct.unpack('>I', len_bytes)[0]
            
            # Sanity check size (e.g. < 64MB)
            if length > 0 and length < 64*1024*1024:
                print(f"Found zImage at offset {start} (0x{start:x}), size {length}")
                if out_zimage:
                    with open(out_zimage, 'wb') as out:
                        out.write(data[start:start+length])
                    print(f"Extracted zImage to {out_zimage}")
                zimage_found = True
                break
        except:
            pass
        offset = idx + 1

    # --- Extract DTB ---
    # FDT magic is 0xd00dfeed (Big Endian)
    fdt_magic = b'\xd0\x0d\xfe\xed'
    
    offset = 0
    dtb_found = False
    while True:
        idx = data.find(fdt_magic, offset)
        if idx == -1:
            print("DTB magic not found (other than header).")
            break
            
        # Skip the FIT image header itself (offset 0)
        if idx == 0:
            offset = idx + 1
            continue
            
        try:
            # Check for DTB property length (8 bytes before data)
            len_bytes = data[idx-8:idx-4]
            length = struct.unpack('>I', len_bytes)[0]
            
            # Sanity check size (e.g. < 1MB)
            if length > 0 and length < 1024*1024:
                # Verify totalsize in FDT header matches
                fdt_totalsize = struct.unpack('>I', data[idx+4:idx+8])[0]
                
                if fdt_totalsize == length:
                    print(f"Found embedded DTB at offset {idx} (0x{idx:x}), size {length}")
                    if out_dtb:
                        with open(out_dtb, 'wb') as out:
                            out.write(data[idx:idx+length])
                        print(f"Extracted DTB to {out_dtb}")
                    dtb_found = True
                    break # Assuming the first embedded DTB is the one we want
        except:
            pass
        offset = idx + 1

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: extract_components.py <image.ub> <out_zimage> <out_dtb>")
        sys.exit(1)
        
    extract_components(sys.argv[1], sys.argv[2], sys.argv[3])
