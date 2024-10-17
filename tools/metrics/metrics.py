from recorder_viz import RecorderReader

class Stats:
    def __init__(self):
        self.call_name = None
        self.total_calls = 0
        self.total_bytes = 0
        self.total_time = 0
        self.api = None


if __name__ == "__main__":
    posix_meta_operations = []
    posix_file_read_operations = ["read", "pread", "pread64", "fread", "readv"]
    posix_file_write_operations = ["write", "pwrite", "pwrite64", "fwrite", "writev"]
    posix_meta_operations = [op for op in posix_file_read_operations if op not in posix_file_write_operations or op not in posix_meta_operations]

    reader = RecorderReader("/p/lustre3/zhu22/traces/hdf5-1.14.4-3-recorder-traces/194528.660-lassen378-wang116-lt-t_pflush2-173065")
    posix_calls = {}
    for rank in range(reader.GM.total_ranks):
        records = reader.records[rank]
        for i in range(reader.LMs[rank].total_records):
            record = records[i]
            func_name = reader.funcs[record.func_id]
            if func_name in posix_calls:
                stats = posix_calls[func_name]
                stats.total_calls += 1
                stats.total_bytes += record.bytes
                stats.total_time += (record.end_time - record.start_time)
            else:
                posix_calls[func_name] = Stats(func_name, 1, record.bytes, record.end_time - record.start_time)
    print(posix_calls)

    # for rank in range(reader.GM.total_ranks):
    #     records = reader.records[rank]
    #     for i in range(reader.LMs[rank].total_records):
    #         record = records[i]
    #         func = reader.funcs[record.func_id]
    #         print(func)




    