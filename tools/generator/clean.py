import sys
with open(sys.argv[1]) as f:
    lines = f.readlines()
    for line in lines:
        if "H5FD_" not in line:
            print(line.replace("\n",""))
