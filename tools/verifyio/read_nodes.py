#!/usr/bin/env python
# encoding, utf-8
from itertools import repeat
from verifyio_graph import VerifyIONode

accepted_mpi_funcs = [
 'MPI_Send', 'MPI_Ssend', 'MPI_Isend',          
 'MPI_Recv', 'MPI_Sendrecv', 'MPI_Irecv',
 'MPI_Wait', 'MPI_Waitall', 'MPI_Waitany',
 'MPI_Waitsome', 'MPI_Test', 'MPI_Testall',
 'MPI_Testany', 'MPI_Testsome', 'MPI_Bcast',
 'MPI_Ibcast', 'MPI_Reduce', 'MPI_Ireduce',
 'MPI_Gather', 'MPI_Igather', 'MPI_Gatherv',
 'MPI_Igatherv', 'MPI_Barrier' 'MPI_Alltoall',
 'MPI_Allreduce', 'MPI_Allgatherv', 
 'MPI_Reduce_scatter', 'MPI_File_open',
 'MPI_File_close', 'MPI_File_read_at_all',
 'MPI_File_write_at_all', 'MPI_File_set_size',
 'MPI_File_set_view', 'MPI_File_sync', 
 'MPI_File_read_all', 'MPI_File_read_ordered',
 'MPI_File_write_all','MPI_File_write_ordered',
 'MPI_Comm_dup', 'MPI_Comm_split',
 'MPI_Comm_split_type', 'MPI_Cart_create',
 'MPI_Cart_sub'
]

def read_mpi_nodes(reader):
    nprocs = reader.GM.total_ranks
    mpi_nodes = [[] for i in repeat(None, nprocs)]

    func_list = reader.funcs
    for rank in range(nprocs):
        records = reader.records[rank]
        for seq_id in range(reader.LMs[rank].total_records):
            func = func_list[records[seq_id].func_id]
            if func in accepted_mpi_funcs:
                mpi_node = VerifyIONode(rank, seq_id, func)
                mpi_nodes[rank].append(mpi_node)

    return mpi_nodes

'''
Read confliciing pairs from a file
Each line of the file should have the following format,
    func-rank1-seqId1, func-rank2-seqId2

Return:
    nodes: conflicting I/O accesses of type VerifyIONode
    pairs: list of [c1, c2], where c1 and c2 are VerifyIONode
'''
def read_io_nodes(reader, path):
    exist_nodes = set()

    nprocs = reader.GM.total_ranks
    io_nodes = [[] for i in repeat(None, nprocs)]
    pairs = []

    f = open(path, "r")
    lines = f.readlines()
    f.close()

    for line in lines:
        pair = line.replace(" ", "").replace("\n", "").split(",")

        if pair[0] not in exist_nodes:
            tmp = pair[0].split("-")
            func, rank, seq_id = tmp[0], int(tmp[1]), int(tmp[2])
            c1 = VerifyIONode(rank, seq_id, func);
            io_nodes[rank].append(c1)
            exist_nodes.add(pair[0])

        if pair[1] not in exist_nodes:
            tmp = pair[1].split("-")
            func, rank, seq_id = tmp[0], int(tmp[1]), int(tmp[2])
            c2 = VerifyIONode(rank, seq_id, func);
            io_nodes[rank].append(c2)
            exist_nodes.add(pair[1])

        # To test for properly synchonization
        # We can ignore the conflicting pair on
        # same node.
        if c1.rank != c2.rank:
            pairs.append([c1, c2])

    return io_nodes, pairs
