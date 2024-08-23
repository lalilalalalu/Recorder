import sys
from itertools import repeat
from verifyio_graph import VerifyIONode, MPICallType
import read_nodes

class MPIEdge:
    def __init__(self, call_type, head=None, tail=None):
        # Init head/tail according the cal type
        self.call_type = call_type  # enum of MPICallType
        if call_type is MPICallType.ALL_TO_ALL:
            self.head, self.tail = [], []
        if call_type is MPICallType.ONE_TO_MANY:
            self.head, self.tail = None, []
        if call_type is MPICallType.MANY_TO_ONE:
            self.head, self.tail = [], None
        if call_type is MPICallType.POINT_TO_POINT:
            self.head, self.tail = None, None
        # override if supplied
        if head:
            self.head = head        # list/instance of VerifyIONode
        if tail:
            self.tail = tail        # list/instance of VerifyIONode

class MPICall:
    def __init__(self, rank, seq_id, func, **kwargs):
        self.rank    = int(rank)
        self.seq_id  = int(seq_id)             # Sequence Id of the mpi call
        self.func    = str(func)
        self.matched = False
        for k, v in kwargs.items():
            val = v
            if k in ["src", "dst", "stag", "rtag"]:
                val = int(v)
            setattr(self, k, val)

    def get_key(self):
        # self.comm for calls like MPI_Bcast, MPI_Barier
        # self.mpifh for calls like MPI_File_close
        key = self.func + ";" + str(getattr(self, 'comm', '')) + ";" + str(getattr(self, 'mpifh', ''))
        return key

    def is_blocking_call(self):
        if self.func.startswith("MPI_I"):
            return False
        return True


class MPIMatchHelper:
    def __init__(self, reader, mpi_sync_calls):
        self.recorder_reader = reader
        self.num_ranks       = reader.nprocs
        self.all_mpi_calls   = [[] for i in repeat(None, self.num_ranks)]

        self.recv_calls      = [[[] for i in repeat(None, self.num_ranks)] for j in repeat(None, self.num_ranks)]
        self.send_calls      = [0 for i in repeat(None, self.num_ranks)]
        self.wait_test_calls = [[] for i in repeat(None, self.num_ranks)]
        self.coll_calls      = [{} for i in repeat(None, self.num_ranks)]

        self.send_func_names   = ['MPI_Send','MPI_Ssend', 'MPI_Isend','MPI_Sendrecv']
        self.recv_func_names   = ['MPI_Recv', 'MPI_Irecv', 'MPI_Sendrecv']

        # According to the MPI standard, not all collective calls serve the purpose
        # of synchornzations, i.e., many of them do not impose order
        if mpi_sync_calls:
            self.bcast_func_names  = []
            self.redgat_func_names = ['MPI_Reduce_scatter', 'MPI_Reduce_scatter_block']
            self.alltoall_func_names   = ['MPI_Barrier', 'MPI_Allgather', 'MPI_Alltoall', 'MPI_Alltoallv', 'MPI_Alltoallw', 'MPI_Allreduce']
        else:
            self.bcast_func_names  = ['MPI_Bcast', 'MPI_Ibcast']
            self.redgat_func_names = ['MPI_Reduce', 'MPI_Ireduce', 'MPI_Gather', 'MPI_Igather', 'MPI_Gatherv', 'MPI_Igatherv']
            self.alltoall_func_names = ['MPI_Barrier', 'MPI_Allreduce', 'MPI_Allgatherv', 'MPI_Allgatherv', 'MPI_Alltoall',
                    'MPI_Reduce_scatter', 'MPI_File_open', 'MPI_File_close', 'MPI_File_read_all',
                    'MPI_File_read_at_all', 'MPI_File_read_order', 'MPI_File_write_all', 'MPI_File_write_at_all',
                    'MPI_File_write_ordered', 'MPI_File_set_size', 'MPI_File_set_view', 'MPI_File_sync',
                    'MPI_Comm_dup', 'MPI_Comm_split', 'MPI_Comm_split_type', 'MPI_Cart_create', 'MPI_Cart_sub']

        self.translate_table = self.__generate_translation_table()

    def is_send_call(self, func_name):
        if func_name in self.send_func_names:
            return True
        return False

    def is_recv_call(self, func_name):
        if func_name in self.recv_func_names:
            return True
        return False

    def is_coll_call(self, func_name):
        if func_name in self.alltoall_func_names or func_name in self.bcast_func_names \
                or func_name in self.redgat_func_names:
            return True
        return False

    def call_type(self, func_name):
        if self.is_send_call(func_name):
            return MPICallType.POINT_TO_POINT
        if func_name in self.alltoall_func_names:
            return MPICallType.ALL_TO_ALL
        if func_name in self.bcast_func_names:
            return MPICallType.ONE_TO_MANY
        if func_name in self.redgat_func_names:
            return MPICallType.MANY_TO_ONE
        return MPICallType.OTHER

    def read_one_mpi_call(self, rank, seq_id, record):
        func = self.recorder_reader.funcs[record.func_id]
        func_args_map = {
            'MPI_Send':     ['dst', 'stag', 'comm'],
            'MPI_Ssend':    ['dst', 'stag', 'comm'],
            'MPI_Isend':    ['dst', 'stag', 'comm'],
            'MPI_Recv':     ['src', 'rtag', 'comm'],
            'MPI_Sendrecv': ['src', 'dst', 'stag', 'rtag', 'comm'],
            'MPI_Irecv':    ['src', 'rtag', 'comm', 'req'],
            'MPI_Wait':     ['req', 'src', 'rtag'],
            'MPI_Waitall':  ['req'],
            'MPI_Waitany':  ['req', 'tindx'],
            'MPI_Waitsome': ['req', 'reqflag', 'tindx'],
            'MPI_Test':     ['req', 'reqflag', 'src', 'rtag'],
            'MPI_Testall':  ['req', 'reqflag'],
            'MPI_Testany':  ['req', 'reqflag', 'tindx'],
            'MPI_Testsome': ['req', 'reqflag', 'tindx'],
            'MPI_Bcast':    ['src', 'comm'],
            'MPI_Ibcast':   ['src', 'comm', 'req'],
            'MPI_Reduce':   ['src', 'comm'],
            'MPI_Ireduce':  ['src', 'comm', 'req'],
            'MPI_Gather':   ['src', 'comm'],
            'MPI_Igather':  ['src', 'comm', 'req'],
            'MPI_Gatherv':  ['src', 'comm'],
            'MPI_Igatherv': ['src', 'comm', 'req'],
            'MPI_Barrier':          ['comm'],
            'MPI_Alltoall':         ['comm'],
            'MPI_Allreduce':        ['comm'],
            'MPI_Allgatherv':       ['comm'],
            'MPI_Reduce_scatter':   ['comm'],
            'MPI_Comm_dup':         ['comm'],
            'MPI_Comm_split':       ['comm'],
            'MPI_Comm_split_type':  ['comm'],
            'MPI_Cart_create':      ['comm'],
            'MPI_Cart_sub':         ['comm'],
            'MPI_File_open':        ['mpifh'],
            'MPI_File_close':       ['mpifh'],
            'MPI_File_read_at_all': ['mpifh'],
            'MPI_File_write_at_all':['mpifh'],
            'MPI_File_set_size':    ['mpifh'],
            'MPI_File_set_view':    ['mpifh'],
            'MPI_File_sync':        ['mpifh'],
            'MPI_File_read_all':    ['mpifh'],
            'MPI_File_read_ordered':['mpifh'],
            'MPI_File_write_all':   ['mpifh'],
            'MPI_File_write_ordered':['mpifh'],
        }
        if func in func_args_map:
            #args = []
            #for i in range(record.arg_count):
            #    arg = record.args[i].decode("utf-8", "ignore")
            #    args.append(arg)
            args = [record.args[i].decode("utf-8","ignore") for i in range(record.arg_count)]
            arg_names = func_args_map[func]
            mapped_args = dict(zip(arg_names, args))
            return MPICall(rank, seq_id, func, **mapped_args)
        else:
            print(f"{func} not found in func_args_map")
            return MPICall(rank, seq_id, func)

    def mpi_status_to_src_tag(self, status_str):
            if str(status_str).startswith("["):
                return status_str[1:-1].split("_")[0], status_str[1:-1].split("_")[1]
            else:   # MPI_STATUS_IGNORE
                return 0, 0

    # Go through every record in the trace and preprocess
    # the mpi calls, so they can be matched later.
    def read_mpi_calls(self, reader):
        for rank in range(self.num_ranks):
            records = reader.records[rank]
            for seq_id in range(reader.num_records[rank]):

                func_name = reader.funcs[records[seq_id].func_id]
                if func_name not in read_nodes.accepted_mpi_funcs: continue

                mpi_call = self.read_one_mpi_call(rank, seq_id, records[seq_id])

                self.all_mpi_calls[rank].append(mpi_call)

                # Note here the index is not the same as
                # seq id. Seq id the index in trace records.
                # The index here is the index of all_mpi_calls
                # without gap.
                index = len(self.all_mpi_calls[rank]) - 1

                if self.is_coll_call(func_name):
                    key = mpi_call.get_key()
                    if key in self.coll_calls[rank]:
                        self.coll_calls[rank][key].append(index)
                    else:
                        self.coll_calls[rank][key] = [index]
                if self.is_send_call(func_name):
                    self.send_calls[rank] += 1
                if self.is_recv_call(func_name):
                    global_src = self.local2global(mpi_call.comm, mpi_call.src)
                    self.recv_calls[rank][global_src].append(index)
                if func_name.startswith("MPI_Wait") or func_name.startswith("MPI_Test"):
                    self.wait_test_calls[rank].append(index)


    def __generate_translation_table(self):
        func_list = self.recorder_reader.funcs

        translate = {}
        translate['MPI_COMM_WORLD'] = range(self.num_ranks)

        for rank in range(self.num_ranks):
            records = self.recorder_reader.records[rank]
            for i in range(self.recorder_reader.num_records[rank]):
                record = records[i]
                func = func_list[record.func_id]

                comm, local_rank, world_rank = None, rank, rank

                if func in ['MPI_Comm_split', 'MPI_Comm_split_type', 'MPI_Comm_dup', \
                            'MPI_Cart_create' 'MPI_Comm_create', 'MPI_Cart_sub']:
                    comm = record.args[0].decode("utf-8", "ignore")
                    local_rank = int(record.args[1])

                if comm:
                    if comm not in translate:
                        translate[comm] = list(range(self.num_ranks))
                    translate[comm][local_rank] = world_rank
        return translate

    # Communicator local rank to global rank
    def local2global(self, comm, local_rank):
        return self.translate_table[comm][local_rank]


def find_wait_test_call(req, rank, helper, need_match_src_tag=False, src=0, tag=0):

    found = None

    # We don't check the flag here
    # We are certain that calls in wait_test_calls all have completed
    # rquests
    for idx in helper.wait_test_calls[rank]:
        wait_call = helper.all_mpi_calls[rank][idx]
        func = wait_call.func

        if (func == 'MPI_Wait' or func == 'MPI_Waitall') or \
           (func == 'MPI_Test' or func == 'MPI_Testall') :
            if req in wait_call.req:
                if need_match_src_tag:
                    if src==wait_call.src and tag==wait_call.rtag:
                        found = wait_call
                else:
                    found = wait_call

                if found:
                    wait_call.req.remove(req)
                    if(len(wait_call.req) == 0):
                        helper.wait_test_calls[rank].remove(idx)
                    break

        elif func == 'MPI_Waitany' or func == 'MPI_Testany':
            if req in wait_call.req:
                inx = wait_call.req.index(req)
                if str(inx) in wait_call.tindx:
                    found = wait_call
                    helper.wait_test_calls[rank].remove(idx)
                    break

        elif func == 'MPI_Waitsome' or func == 'MPI_Testsome':
            if req in wait_call.req:
                inx = wait_call.req.index(req)
                if str(inx) in wait_call.tindx:
                    found = wait_call
                    wait_call.req.remove(req)
                    wait_call.tindx.remove(str(inx))
                    if len(wait_call.tindx) == 0:
                        helper.wait_test_calls[rank].remove(idx)
                    break
    return found


def match_collective(mpi_call, helper):

    def add_nodes_to_edge(edge, call):
        node = VerifyIONode(call.rank, call.seq_id, call.func)
        if "MPI_File" in call.func: node.mpifh = call.mpifh

        # All-to-all (alltoall, barrier, etc.)
        if edge.call_type == MPICallType.ALL_TO_ALL:
            edge.head.append(node)
            edge.tail.append(node)
        # One-to-many (bcast)
        if edge.call_type == MPICallType.ONE_TO_MANY:
            if call.rank == helper.local2global(call.comm, call.src):
                edge.head = node
            else:
                edge.tail.append(node)
        # Many-to-one (reduce)
        if edge.call_type == MPICallType.MANY_TO_ONE:
            if call.rank == helper.local2global(call.comm, call.src):
                edge.tail = node
            else:
                edge.head.append(node)

    # All matching collective calls have the same name
    # and thus the same call type
    call_type = helper.call_type(mpi_call.func)
    edge = MPIEdge(call_type)

    for rank in range(helper.num_ranks):

        key = mpi_call.get_key()

        # this rank has not made this particular
        # collective call
        if key not in helper.coll_calls[rank]:
            continue

        # this rank has made this collective call
        coll_call_index = helper.coll_calls[rank][key][0]
        coll_call       = helper.all_mpi_calls[rank][coll_call_index]

        # blocking vs. non-blocking
        if mpi_call.is_blocking_call():
            add_nodes_to_edge(edge, coll_call)
        else:
            wait_call = find_wait_test_call(coll_call.req, rank, helper)
            if wait_call:
                add_nodes_to_edge(edge, wait_call)

        # If no more collective calls have the same key
        # then remove this key from the dict
        helper.coll_calls[rank][key].pop(0)
        if(len(helper.coll_calls[rank][key]) == 0):
            helper.coll_calls[rank].pop(key)

        # Set this collective call as matched
        # so we don't do repeat work when later
        # exam this call
        coll_call.matched = True

    mpi_call.matched = True
    return edge

def match_pt2pt(send_call, helper):

    head_node = VerifyIONode(send_call.rank, send_call.seq_id, send_call.func)
    tail_node = None

    comm = send_call.comm
    global_dst =  helper.local2global(comm, send_call.dst)
    global_src = send_call.rank

    for recv_call_idx in helper.recv_calls[global_dst][global_src]:
        recv_call = helper.all_mpi_calls[global_dst][recv_call_idx]

        # Check for comm, src, and tag.
        if recv_call.comm != comm: continue

        if (recv_call.rtag == send_call.stag or recv_call.rtag == ANY_TAG):
            if recv_call.is_blocking_call():
                # we don't really need to set this because 
                # we always start matching from send calls
                # and we use helper.recv_calls[][] to key
                # track of unmatched recv calls.
                recv_call.matched = True
                tail_node = VerifyIONode(recv_call.rank, recv_call.seq_id, recv_call.func)
            else:
                if recv_call.rtag == ANY_TAG or global_src == ANY_SOURCE:
                    wait_call = find_wait_test_call(recv_call.req, global_dst, helper, True, send_call.rank, send_call.stag)
                else:
                    wait_call = find_wait_test_call(recv_call.req, global_dst, helper)
                if wait_call:
                    tail_node = VerifyIONode(wait_call.rank, wait_call.seq_id, wait_call.func)

        if tail_node:
            helper.recv_calls[global_dst][global_src].remove(recv_call_idx)
            break

    if tail_node :
        send_call.matched = True
        edge = MPIEdge(MPICallType.POINT_TO_POINT, head_node, tail_node)
        #print("match pt2pt: %s --> %s" %(edge.head, edge.tail))
        return edge
    else:
        print("Warnning: unmatched send call:", head_node, global_dst, send_call.stag)
        return None


'''
mpi_sync_calls=True will include only the calls
that guarantee synchronization, this flag is used
for checking MPI semantics
'''
def match_mpi_calls(reader, mpi_sync_calls=False):
    edges = []
    helper = MPIMatchHelper(reader, mpi_sync_calls)
    helper.read_mpi_calls(reader)

    for rank in range(helper.num_ranks):
        for mpi_call in helper.all_mpi_calls[rank]:
            edge = None
            if mpi_call.matched:
                continue
            if helper.is_coll_call(mpi_call.func):
                edge = match_collective(mpi_call, helper)
            if helper.is_send_call(mpi_call.func):
                edge = match_pt2pt(mpi_call, helper)
            if edge:
                edges.append(edge)

    # validate result
    for rank in range(helper.num_ranks):
        recvs_sum = 0
        for i in range(helper.num_ranks):
            recvs_sum += len(helper.recv_calls[rank][i])
        if recvs_sum:
            print("Rank %d has %d unmatched recvs" %(rank, recvs_sum))
        if len(helper.coll_calls[rank]) != 0:
            print("Rank %d has %d unmatched colls" %(rank, len(helper.coll_calls[rank])))
        # No need to report unmatched test/wait calls. For example,
        # test calls with some MPI_REQUEST_NULL as input requrests may
        # not be set to matched and removed from the list
        # if len(helper.wait_test_calls[rank]) != 0:
        #    print("Rank %d has %d unmatched wait/test" %(rank, len(helper.wait_test_calls[rank])))

    # Debug output:
    # print out the nead nodes of each edge
    """
    for i in range(len(edges)):
        edge = edges[i]
        print("Head nodes of edge: ", i)
        if isinstance(edge.head, list):
            for n in edge.head:
                print("\t", n)
        else:
            print("\t", edge.head)
        print("")
    """

    return edges
