import argparse, time, sys
from recorder_reader import RecorderReader
from read_nodes import read_mpi_nodes, read_io_nodes
from match_mpi import match_mpi_calls
from verifyio_graph import VerifyIONode, VerifyIOGraph

"""
Verify for a given conflicting pair of (c1, c2),
whether c1 happens-before c2 for the given consistency
semantics.

For POSIX semantics:
    - check c1 hb-> c2
For Commit semantics:
    - check c1 hb-> commit hb-> c2
For Session semantics:
    - check if c1 po-> close-hb-open po-> c2
For MPI-IO semantics:
    - check if c1 po-> sync-hb-sync po-> c2

We have four alogorithms:
Algo 1. Graph Reachibility (e.g., DFS)
Algo 2. Transitivive Closure
Algo 3. Vector Clock
Algo 4. On-the-fly MPI Check

TODO: choose algorithm dynamically
"""
def verify_pair_proper_synchronization(n1, n2, G, semantics, algo):

    v1, v2 = None, None

    if semantics == "POSIX":
        v1 = n1
        v2 = n2
    elif semantics == "Commit":
        v1 = G.next_po_node(n1, ["fsync", "close", "MPI_File_sync", "MPI_File_close"])
        # v1 = G.next_hb_node(n1, ["fsync", "close"], rank)
        v2 = n2
    elif semantics == "Session":
        v1 = G.next_po_node(n1, ["close", "fsync"])
        v2 = G.prev_po_node(n2, ["open",  "fsync"])
    elif semantics == "MPI-IO":
        v1 = G.next_po_node(n1, ["MPI_File_close", "MPI_File_sync"])
        v2 = G.prev_po_node(n2, ["MPI_File_open",  "MPI_File_sync"])
    
    if (not v1) or (not v2):
        return False

    # Algorithm 1: Graph Reachibility
    if algo == 1:
        return G.has_path(v1, v2)

    # Algorithm 2: Transitivive
    if algo == 2:
        print("transitive closure is alwasy slower than vector clock")
        print("use vector clock instead")
        algo = 3
        pass

    # Algorithm 3: Vector Clock
    if algo == 3:
        vc1 = G.get_vector_clock(v1)
        vc2 = G.get_vector_clock(v2)
        return (bool)(vc1[v1.rank] < vc2[v1.rank])

    # Algorithm 4: On-the-fly MPI check
    if algo == 4:
        return False


"""
For an execution, iterate through every conflicting pairs
and verify if each conflict pair is properly synchornized for
the given consistency semantics

 - conflict_pairs: list of (n1, n2s)
    n1: conflicting I/O operation (VerifyIONode)
    n2s: list of I/O operations conflicting with n1.
 - G: the happens-before graph (VerifyIOGraph)
 - args: users input arguments
"""
def verify_execution_proper_synchronization(conflict_pairs, G, args):

    total_conflicts = 0
    total_violations = 0

    summary = {
        'c_ranks_cnt': [[0 for _ in range(reader.nprocs)] for _ in range(reader.nprocs)],
        'c_files_cnt': {},
        'c_functions_cnt': {}
    }

    for pair in conflict_pairs:

        # n1: conflict I/O operation (VerifyIONode)
        # n2s: conflicting I/O operations (array of VerifyIONode)
        n1, n2s = pair[0], pair[1]

        s = 0
        for i in range(len(n2s)):
            s += len(n2s[i])
        debug_str = f"Verify: {n1} {s} pairs"
        t1 = time.time()

        for rank in range(len(n2s)):
            if len(n2s[rank]) < 1: continue
            total_conflicts += len(n2s[rank])

            # check if n1 happens-before the first node of n2s[rank]
            # if n1 hb-> n2s[rank][0], then n1 hb-> n2s[rank][:]
            if verify_pair_proper_synchronization(n1, n2s[rank][0], G, args.semantics, args.algorithm):
                debug_str += f", n1 -> n2s[{rank}][0]"
                continue

            # check if the last node of n2s[rank] happens-beofre n1
            # if n2s[rank][-1] hb->hb, then n2s[rank][:] hb-> n1
            if verify_pair_proper_synchronization(n2s[rank][-1], n1, G, args.semantics, args.algorithm):
                debug_str += f", n2s[{rank}][-1] -> n1"
                continue

            # check if n1 happens-before the last node of n2s[rank]
            # if not, then n1 is certainly not ->hb any nodes of n2s[rank]
            # similarly, if n2s[rank][0] does not happen-before n1, then
            # non of n2s[rank] will happen-before n1.
            if (not verify_pair_proper_synchronization(n1, n2s[rank][-1], G, args.semantics, args.algorithm)) \
                and (not verify_pair_proper_synchronization(n2s[rank][0], n1, G, args.semantics, args.algorithm)):
                total_violations += len(n2s[rank])
                continue

            # now we are here, its very likely that n1 is not
            # properly-synchornized with any node of n2s[rank],
            # but we still need to go through evey pair to make sure.
            # TODO we could do the previous three checks recursively.
            for n2 in n2s[rank]:
                this_pair_ok = (verify_pair_proper_synchronization(n1, n2, G, args.semantics, args.algorithm) or \
                                verify_pair_proper_synchronization(n2, n1, G, args.semantics, args.algorithm))
                if not this_pair_ok:
                    if args.show_summary:
                        get_violation_info([n1, n2], reader, summary, args, this_pair_ok)
                    total_violations += 1
                    debug_str = f"{n1} {n2}"
                    print(debug_str)

        t2 = time.time()
        #print(debug_str+", time: %.3f" %(t2-t1))

    if args.show_summary:
        print_summary(summary)
    print("Total semantic violations: %d" %total_violations)
    print("Total conflict pairs: %d" %total_conflicts)


def get_shortest_path(G:VerifyIOGraph, src:VerifyIONode, dst:VerifyIONode):
    path = G.shortest_path(src, dst)
    path_str = ""
    for i in range(len(path)):
        node = path[i]
        path_str += str(node) 
        if i != len(path) - 1:
            path_str += "->"
    return path_str


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

def get_violation_info(nodes: list, reader: RecorderReader, summary, args, this_pair_ok):

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
        if args.show_details:
            r_str = build_call_chain_str(right_call_chain, reader)
            l_str = build_call_chain_str(reversed(left_call_chain), reader)
            print(f"{nodes[0]}: {l_str} <--> {nodes[1]}: {r_str} on file {file}, properly synchronized: {this_pair_ok}")


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("traces_folder")
    parser.add_argument("--semantics", type=str, choices=["POSIX", "MPI-IO", "Commit", "Session"],
                        default="MPI-IO", help="Verify if I/O operations are properly synchronized under the specific semantics")
    parser.add_argument("--algorithm", type=int, choices=[1, 2, 3, 4],
                        default=3, help="1: graph reachibility, 2: transitive closure, 3: vector clock, 4: on-the-fly MPI check")
    parser.add_argument("--show_details", action="store_true", help="Show details of the conflicts")
    parser.add_argument("--show_summary", action="store_true", help="Show summary of the conflicts")
    args = parser.parse_args()

    #import psutil
    #print('1. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    t1 = time.time()
    reader = RecorderReader(args.traces_folder)
    #print('2. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    mpi_nodes = read_mpi_nodes(reader)
    #print('3. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    io_nodes, conflict_pairs = read_io_nodes(reader, args.traces_folder+"/conflicts.txt")
    t2 = time.time()
    print("Step 1. read trace records and conflicts time: %.3f secs" %(t2-t1))
    #print('4. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    all_nodes = mpi_nodes
    for rank in range(reader.nprocs):
        all_nodes[rank] += io_nodes[rank]
        all_nodes[rank] = sorted(all_nodes[rank], key=lambda x: x.seq_id)
    #print('5. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    # get mpi calls and matched edges
    t1 = time.time()
    mpi_edges = match_mpi_calls(reader)
    t2 = time.time()
    #print('6. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)
    print("Step 2. match mpi calls: %.3f secs, mpi edges: %d" %((t2-t1),len(mpi_edges)))

    t1 = time.time()
    G = VerifyIOGraph(all_nodes, mpi_edges, include_vc=True)
    t2 = time.time()
    print("Step 3. build happens-before graph: %.3f secs, nodes: %d" %((t2-t1), G.num_nodes()))
    #print('7. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)

    # Correct code (traces) should generate a DAG without any cycles
    if G.check_cycles(): quit()

    t1 = time.time()
    G.run_vector_clock()
    #G.run_transitive_closure()
    t2 = time.time()
    print("Step 4. run vector clock algorithm: %.3f secs" %(t2-t1))
    #print('8. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)
    # G.plot_graph("vgraph.jpg")

    t1 = time.time()
    verify_execution_proper_synchronization(conflict_pairs, G, args)
    t2 = time.time()
    print("Step 5. %s semantics verification time: %.3f secs" %(args.semantics, t2-t1))
    #print('9. RAM Used (GB):', psutil.virtual_memory()[3]/1000000000)
