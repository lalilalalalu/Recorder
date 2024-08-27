import argparse, time, sys

from networkx import edges

from recorder_reader import RecorderReader
from read_nodes import read_mpi_nodes, read_io_nodes
from match_mpi import match_mpi_calls
from verifyio_graph import VerifyIONode, VerifyIOGraph, MPICallType

'''
def verify_proper_synchronization(G, S, R, pairs):
    O = [0 * len(S)]

    # for every conflict pair
    for pair in pairs:
        n1 = graph_node_key(pair[0])
        n2 = graph_node_key(pair[1])

        # from left to right, for every S[i]
        # narrow it down to a single op O[i]
        n_left = n1
        for i in range(0, len(S), 1):
            if R[i] == "po":
                O[i] = next_op(n_left, S[i])
                # Todo: if O[i] is empty
                # then the conflict must be
                # not properly synchornized
                n_left = O[i]
            else:
                break
    
        # from right to left, for every S[i]
        # narrow it down to a single op O[i]
        n_right = n2
        for i in range(len(S), 0, -1):
            if R[i+1] == "po":
                O[i] = perv_op(n_right, S[i])
                n_right = O[i]
            else:
                break

        # then do the final pass
        n_left = n1
        for i in range(0, len(S), 1):
            # already narrowed down
            if O[i]:
                n_left = O[i]
                continue
            # hb
            for o1 in n_left:
                for every rank:
                    o2 = next_op(None, S[i])
                    if tc[o1, o2]:
                        O[i] += o2
            if not O[i]:
                return

'''


def verify_posix_semantics(G, conflict_pairs):

    def check_pair_in_order(n1, n2):
        if G.has_path(n1, n2):
            #print(get_shortest_path(G, n1, n2))
            return True
        return False

    properly_synchronized = True

    for pair in conflict_pairs:
        n1 = pair[0]
        n2 = pair[1]
        this_pair_ok = (check_pair_in_order(n1, n2) or check_pair_in_order(n2, n1))
        print("%s <--> %s, properly synchronized: %s" %(n1, n2, this_pair_ok))
        if not this_pair_ok:
            properly_synchronized = False

    return properly_synchronized

def get_shortest_path(G, src, dst):
    path = G.shortest_path(src, dst)
    path_str = ""
    for i in range(len(path)):
        node = path[i]
        path_str += str(node) 
        if i != len(path) - 1:
            path_str += "->"
    return path_str

def verify_session_semantics(G, conflict_pairs, 
                             close_ops=["close", "fclose"],
                             open_ops=["open", "fopen"], reader=None):
    def check_pair_in_order(n1 , n2):
        next_sync = G.next_po_node(n1, close_ops)
        prev_sync = G.prev_po_node(n2, open_ops)
        inorder = False

        # Algorithm 1: Reachibility
        # inorder = (bool) ( (next_sync and prev_sync) and G.has_path(next_sync, prev_sync) )

        # Algorithm 2: Vector Clock
        if next_sync and prev_sync:
            vc1 = G.get_vector_clock(next_sync)
            vc2 = G.get_vector_clock(prev_sync)
            inorder = (bool) (vc1[next_sync.rank] < vc2[next_sync.rank])

        # if inorder:
        #   path_str = get_shortest_path(G, next_sync, prev_sync)
        #   print("%s -> %s -> %s" %(n1, path_str, n2))
        return inorder

    properly_synchronized = True
    # total = len(conflict_pairs)
    i = 0
    summary = {
        'c_ranks_cnt': [[0 for _ in range(reader.nprocs)] for _ in range(reader.nprocs)],
        'c_files_cnt': {},
        'c_functions_cnt': {}
    }
    total_conflicts = 0
    for pair in conflict_pairs:

        # print progress
        #sys.stdout.write("%s/%s\r" %(i,total))
        #sys.stdout.flush()

        n1, n2s = pair[0], pair[1]                   # n1:VerifyIONode, n2s[rank]: array of VerifyIONode
        for rank in range(len(n2s)):
            total_conflicts += len(n2s[rank])
            if len(n2s[rank]) < 1: continue
            # check if n1 happens-before the first in n2s[rank]
            # n1 ->hb n2s[rank][0], then n1 ->hb all n2s[rank]
            if check_pair_in_order(n1, n2s[rank][0]):
                continue
            # otherwise, check if last of n2s[rank] happens-beofre n1
            # n2s[rank][-1] ->hb n1, then all n2s[rank] ->hb n1
            if check_pair_in_order(n2s[rank][-1], n1):
                continue

            # now we are here, check for every n2s[rank]
            for n2 in n2s[rank]:
                this_pair_ok = (check_pair_in_order(n1, n2) or check_pair_in_order(n2, n1))
                if not this_pair_ok:
                    if args.show_summary:
                        get_conflict_info([n1, n2], reader, summary, args.show_details, this_pair_ok)
                    i = i + 1
                    properly_synchronized = False


    if args.show_summary:
        print_summary(summary)
    print("Total semantic violations: %d" %i)
    print("Total conflict pairs: %d" %total_conflicts)
    return properly_synchronized

def verify_session_semantics2( conflict_pairs,
                               close_ops=["close", "fclose"],
                               open_ops=["open", "fopen"], reader=None, all_nodes=None, mpi_edges=None):

    def check_pair_in_order(n1, n2):
        """
        if (n2.seq_id == 826 and n1.seq_id == 598) or (n1.seq_id == 1856 and n2.seq_id == 1936):
            print(1234)
        """
        test = map_edges(mpi_edges, reader)
        next_sync = None
        prev_sync = None
        inorder = False
        next_sync_index = -1
        # O(N)
        for idx, call in enumerate(all_nodes[n1.rank]):
            if call.seq_id > n1.seq_id and call.func in close_ops:
                next_sync = call
                next_sync_index = idx
                break
        # O(M)
        for idx, call in enumerate(reversed(all_nodes[n2.rank]), start=1):
            if call.seq_id < n2.seq_id and call.func in open_ops:
                prev_sync = call
                break
        #barrier after next sync (n1) & barrier before prev sync (n2)
        if next_sync and prev_sync:
            # O(N)
            for sc in all_nodes[n1.rank][next_sync_index+1:]:
                # O(E)
                for edge in mpi_edges:
                    # O(H)
                    if edge.call_type == MPICallType.ALL_TO_ALL:
                        for edge_call in edge.head:
                            """
                            if edge_call.seq_id == 1877 or edge_call.seq_id == 614:
                                sd= edge_call.graph_key()
                                ssc= sc.graph_key()
                                print(sd==ssc)
                                test = sd==ssc
                            """
                            if edge_call.graph_key() == sc.graph_key() and len(edge.tail) > n2.rank:
                                if edge.tail[n2.rank].seq_id <= prev_sync.seq_id:
                                    return True
                                return False
                    elif edge.call_type == MPICallType.ONE_TO_MANY:
                        if edge.head.graph_key() == sc.graph_key():
                            for edge_call in edge.tail:
                                if edge_call.seq_id <= prev_sync.seq_id:
                                    return True
                            return False
                    else:
                        for edge_call in edge.head:
                            if edge_call.graph_key() == sc.graph_key():
                                if edge.tail.seq_id <= prev_sync.seq_id:
                                    return True
                #O(N)+O(M)+O(N×E×H)
        return inorder

    def check_pair_in_order3(n1, n2):
        next_sync = None
        prev_sync = None
        next_sync_index = -1
        # O(N)
        for idx, call in enumerate(all_nodes[n1.rank]):
            if call.seq_id > n1.seq_id and call.func in close_ops:
                next_sync = call
                next_sync_index = idx
                break
        # O(M)
        for idx, call in enumerate(reversed(all_nodes[n2.rank]), start=1):
            if call.seq_id < n2.seq_id and call.func in open_ops:
                prev_sync = call
                break
        #barrier after next sync (n1) & barrier before prev sync (n2)
        if next_sync and prev_sync:
            # O(N)
            for sc in all_nodes[n1.rank][next_sync_index+1:]:
                if mpi_edges[n1.rank][sc.seq_id][prev_sync.rank]:
                    if mpi_edges[n1.rank][sc.seq_id][prev_sync.rank].seq_id <= prev_sync.seq_id:
                        return True
                    else:
                        return False
                else:
                    continue

        return False

    properly_synchronized = True
    # total = len(conflict_pairs)
    i = 0
    summary = {
        'c_ranks_cnt': [[0 for _ in range(reader.nprocs)] for _ in range(reader.nprocs)],
        'c_files_cnt': {},
        'c_functions_cnt': {}
    }
    total_conflicts = 0
    for pair in conflict_pairs:

        # print progress
        #sys.stdout.write("%s/%s\r" %(i,total))
        #sys.stdout.flush()

        n1, n2s = pair[0], pair[1]                   # n1:VerifyIONode, n2s[rank]: array of VerifyIONode
        for rank in range(len(n2s)):
            total_conflicts += len(n2s[rank])
            if len(n2s[rank]) < 1: continue
            # check if n1 happens-before the first in n2s[rank]
            # n1 ->hb n2s[rank][0], then n1 ->hb all n2s[rank]
            if check_pair_in_order(n1, n2s[rank][0]):
                continue
            # otherwise, check if last of n2s[rank] happens-beofre n1
            # n2s[rank][-1] ->hb n1, then all n2s[rank] ->hb n1
            if check_pair_in_order(n2s[rank][-1], n1):
                continue

            # now we are here, check for every n2s[rank]
            for n2 in n2s[rank]:
                this_pair_ok = (check_pair_in_order(n1, n2) or check_pair_in_order(n2, n1))
                if not this_pair_ok:
                    if args.show_summary:
                        get_conflict_info([n1, n2], reader, summary, args.show_details, this_pair_ok)
                    i = i + 1
                    properly_synchronized = False


    if args.show_summary:
        print_summary(summary)
    print("Total semantic violations: %d" %i)
    print("Total conflict pairs: %d" %total_conflicts)
    return properly_synchronized

def verify_mpi_semantics2( conflict_pairs,  reader=None, all_nodes=None, mpi_edges=None):
    return verify_session_semantics2(conflict_pairs,
                                     close_ops = ["MPI_File_sync", "MPI_File_close"], \
                                     open_ops  = ["MPI_File_sync", "MPI_File_open"], reader=reader, all_nodes=all_nodes, mpi_edges=mpi_edges)

def verify_mpi_semantics(G, conflict_pairs,  reader=None):
    return verify_session_semantics(G, conflict_pairs,
                                    close_ops = ["MPI_File_sync", "MPI_File_close"], \
                                    open_ops  = ["MPI_File_sync", "MPI_File_open"], reader=reader)

def verify_commit_semantics(G, conflict_pairs):

    def check_pair_in_order(n1, n2):
        this_pair_ok = False
        # TODO need to consider all ranks
        # TODO should check from writer.
        ranks = [n1.rank, n2.rank]
        for rank in ranks:
            next_commit = G.next_hb_node(n1, ["fsync", "close"], rank)
            if (next_commit) and G.has_path(next_commit, n2):
                this_pair_ok = True
                break
        return this_pair_ok

    properly_synchronized = True

    for pair in conflict_pairs:
        n1, n2 = pair[0], pair[1]                   # of VerifyIONode class
        if  not check_pair_in_order(n1, n2):
            properly_synchronized = False
        print("%s <--> %s, properly synchronized: %s" %(n1, n2, this_pair_ok))

    return properly_synchronized

def get_call_chain(node, reader):
    call_chain = []
    seq_id = node.seq_id
    while reader.records[node.rank][seq_id].call_depth > 0:
        call_chain.append(reader.records[node.rank][seq_id])
        seq_id -= 1
    call_chain.append(reader.records[node.rank][seq_id])
    return call_chain

def update_function_count(func_id, summary, reader):
    func_name = reader.funcs[func_id]
    summary['c_functions_cnt'][func_name] = summary['c_functions_cnt'].get(func_name, 0) + 1

def build_call_chain_str(call_chain, reader):
    return "-->".join(reader.funcs[cc.func_id] for cc in call_chain)

def print_summary(summary):
    print("=" * 80)
    print("Details".center(80))
    print("=" * 80)

    print(f"{'Rank':<10} {'Conflicts':<20}")
    print("-" * 30)
    for index, value in enumerate([sum(values) for values in zip(*summary['c_ranks_cnt'])]):
        print(f"{index:<10} {value:<20}")
    print()

    print(f"{'File':<50} {'Conflicts':<20}")
    print("-" * 70)
    for key, count in summary['c_files_cnt'].items():
        print(f"{key:<50} {count:<20}")
    print()

    print(f"{'Function Call':<50} {'Conflicts':<20}")
    print("-" * 70)
    for key, count in summary['c_functions_cnt'].items():
        print(f"{key:<50} {count:<20}")
    print("=" * 80)

def get_conflict_info(nodes: list, reader: RecorderReader, summary=None, show_details=False, this_pair_ok=False):
    left_call_chain = get_call_chain(nodes[0], reader)
    right_call_chain = get_call_chain(nodes[1], reader)
    file = reader.records[nodes[0].rank][nodes[0].seq_id].args[0].decode('utf-8')
    if len(left_call_chain) > 0 and len(right_call_chain) > 0:
        summary['c_ranks_cnt'][nodes[0].rank][nodes[1].rank] += 1
        if file not in summary['c_files_cnt']:
            summary['c_files_cnt'][file] = 0
        summary['c_files_cnt'][file] += 1
        update_function_count(left_call_chain[-1].func_id, summary, reader)
        update_function_count(right_call_chain[-1].func_id, summary, reader)
        if show_details:
            r_str = build_call_chain_str(right_call_chain, reader)
            l_str = build_call_chain_str(reversed(left_call_chain), reader)
            print(f"{nodes[0]}: {l_str} <--> {nodes[1]}: {r_str} on file {file}, properly synchronized: {this_pair_ok}")


def map_edges(mpi_edges, reader):
    # Initialize edges with a list of lists for each rank and sequence ID
    max_record = max(reader.num_records)
    num_ranks = reader.nprocs

    edges = [[] for _ in range(num_ranks)]
    for rank in range(num_ranks):
        edges[rank] = [[] for _ in range(reader.num_records[rank])]


    # Populate the edges list
    for e in mpi_edges:
        if e.call_type == MPICallType.ALL_TO_ALL:
            for edge_call_head in e.head:
                for edge_call_tail in e.tail:
                    edges[edge_call_head.rank][edge_call_head.seq_id].append(edge_call_tail)
        elif e.call_type == MPICallType.ONE_TO_MANY:
            for edge_call_tail in e.tail:
                edges[e.head.rank][e.head.seq_id].append(edge_call_tail)
        else:
            for edge_call_head in e.head:
                edges[edge_call_head.rank][edge_call_head.seq_id].append(e.tail)
    return edges

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("traces_folder")
    #parser.add_argument("conflicts_file")
    parser.add_argument("--semantics", type=str, choices=["POSIX", "MPI-IO", "Commit", "Session"],
                        default="MPI-IO", help="Verify if I/O operations are properly synchronized under the specific semantics")
    parser.add_argument("--show_details", action="store_true", help="Show details of the conflicts")
    parser.add_argument("--show_summary", action="store_true", help="Show summary of the conflicts")
    args = parser.parse_args()

    import psutil
    print('1. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    t1 = time.time()
    reader = RecorderReader(args.traces_folder)
    print('2. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)


    mpi_nodes = read_mpi_nodes(reader)
    print('3. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    io_nodes, conflict_pairs = read_io_nodes(reader, args.traces_folder+"/conflicts.txt")
    t2 = time.time()
    print("Step 1. read trace records and conflicts time: %.3f secs" %(t2-t1))
    print('4. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    all_nodes = mpi_nodes
    for rank in range(reader.nprocs):
        all_nodes[rank] += io_nodes[rank]
        all_nodes[rank] = sorted(all_nodes[rank], key=lambda x: x.seq_id)
    print('5. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)


    # get mpi calls and matched edges
    t1 = time.time()
    mpi_edges = match_mpi_calls(reader)
    t2 = time.time()
    print('6. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)
    print("Step 2. match mpi calls: %.3f secs, mpi edges: %d" %((t2-t1),len(mpi_edges)))


    print('123. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    t1 = time.time()
    G = VerifyIOGraph(all_nodes, mpi_edges, include_vc=True)
    t2 = time.time()
    print("Step 3. build happens-before graph: %.3f secs, nodes: %d" %((t2-t1), G.num_nodes()))
    print('7. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    # Correct code (traces) should generate a DAG without any cycles
    if G.check_cycles(): quit()

    t1 = time.time()
    G.run_vector_clock()
    #G.run_transitive_closure()
    t2 = time.time()
    print("Step 4. run vector clock algorithm: %.3f secs" %(t2-t1))
    print('8. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    # G.plot_graph("vgraph.jpg")
    test = map_edges(mpi_edges, reader)
    t1 = time.time()
    p = True
    if args.semantics == "POSIX":
        p = verify_posix_semantics(G, conflict_pairs)
    elif args.semantics == "MPI-IO":
        # p = verify_mpi_semantics(G, conflict_pairs, reader)
        # p = verify_mpi_semantics2(conflict_pairs=conflict_pairs, reader=reader, all_nodes=all_nodes, mpi_edges=test)
        p = verify_mpi_semantics2(conflict_pairs=conflict_pairs, reader=reader, all_nodes=all_nodes, mpi_edges=mpi_edges)
    elif args.semantics == "Commit":
        p = verify_commit_semantics(G, conflict_pairs)
    elif args.semantics == "Session":
        p = verify_session_semantics(G, conflict_pairs, reader)
    t2 = time.time()
    print("Step 5. verification time: %.3f secs" %(t2-t1))
    if p:
        print("\nProperly synchronized under %s semantics" %args.semantics)
    else:
        print("\nNot properly synchronized under %s semantics" %args.semantics)
    print("verify time: %.3f secs" %(t2-t1))
    print('9. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)
