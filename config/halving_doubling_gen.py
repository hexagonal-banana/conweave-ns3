base_comu_size=1000000 #1MB
flow_count=0
step=1
total_nodes=128
visited=set()
while(step<128):
    for i in range(0,total_nodes):
        if(i not in visited):
            visited.add(i)
            visited.add(i+step)
            flow_count+=2
            print(i,i+step,3,base_comu_size,2)
            print(i+step,i,3,base_comu_size,2)


    assert(len(visited)==total_nodes)
    visited.clear()
    step=step*2
    base_comu_size=base_comu_size*2

print(flow_count)