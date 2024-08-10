#include "ns3/reunion-routing.h"

#include "assert.h"
#include "ns3/assert.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-header.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/settings.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE("ReunionRouting");

namespace ns3{

    ReunionTag::ReunionTag() {}
    ReunionTag::~ReunionTag() {}
    TypeId ReunionTag::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::ReunionTag")
                            .SetParent<Tag>()
                            .AddConstructor<ReunionTag>();
    return tid;
    }
void ReunionTag::SetPathId(uint32_t pathId) {
    m_pathId = pathId;
    }
uint32_t ReunionTag::GetPathId(void) const {
    return m_pathId;
    }
void ReunionTag::SetHopCount(uint32_t hopCount) {
    m_hopCount = hopCount;
    }
uint32_t ReunionTag::GetHopCount(void) const {
    return m_hopCount;
    }
TypeId ReunionTag::GetInstanceTypeId(void) const {
    return GetTypeId();
    }
uint32_t ReunionTag::GetSerializedSize(void) const {
    return sizeof(uint32_t) +
           sizeof(uint32_t);
    }
void ReunionTag::Serialize(TagBuffer i) const {
    i.WriteU32(m_pathId);
    i.WriteU32(m_hopCount);
    }
void ReunionTag::Deserialize(TagBuffer i) {
    m_pathId = i.ReadU32();
    m_hopCount = i.ReadU32();
    }
void ReunionTag::Print(std::ostream& os) const {
    os << "m_pathId=" << m_pathId;
    os << ", m_hopCount=" << m_hopCount;
    }



    ReunionRouting::ReunionRouting(){
        _FlowletTimeout=Time(MicroSeconds(250));
        _SubFlowletGap=Time(MicroSeconds(50));
        m_agingTime=Time(MicroSeconds(50));

        _HighUtilCount=12.5*1000*50*0.9;
        _HighUtilCount=12.5*1000*50*0.7;
    };

    uint64_t ReunionRouting::GetQpKey(uint32_t dip, uint16_t sport, uint16_t dport, uint16_t pg){
         return ((uint64_t)dip << 32) | ((uint64_t)sport << 16) | (uint64_t)pg | (uint64_t)dport;

    }

    void ReunionRouting::SetSwitchInfo(bool isToR, uint32_t switch_id) {
    m_isToR = isToR;
    m_switch_id = switch_id;
    }
    
    uint32_t ReunionRouting::GetOutPortFromPath(const uint32_t& path, const uint32_t& hopCount){
        uint32_t port=((uint8_t*)&path)[hopCount];
        if(_PortTransmit.find(port)==_PortTransmit.end())
            _PortTransmit[port]=0;
        return port;
    };

    uint32_t ReunionRouting::RouteInput(Ptr<Packet> p, CustomHeader ch){
        //NS_LOG_DEBUG("RouteInput Switch ID: "<<m_switch_id);
        Time now = Simulator::Now();

        //trun on event
        if(!m_agingEvent.IsRunning()){
            m_agingEvent = Simulator::Schedule(m_agingTime, &ReunionRouting::AgingEvent, this);
        }

    assert(Settings::hostIp2SwitchId.find(ch.sip) != Settings::hostIp2SwitchId.end());  // Misconfig of Settings::hostIp2SwitchId - sip"
    assert(Settings::hostIp2SwitchId.find(ch.dip) != Settings::hostIp2SwitchId.end());  // Misconfig of Settings::hostIp2SwitchId - dip"
    uint32_t srcToRId = Settings::hostIp2SwitchId[ch.sip];
    uint32_t dstToRId = Settings::hostIp2SwitchId[ch.dip];

    NS_ASSERT_MSG(srcToRId != dstToRId, "Should not be in the same pod");

    // get QpKey to find flowlet
    NS_ASSERT_MSG(ch.l3Prot == 0x11, "Only supports UDP data packets");
    uint64_t qpkey = GetQpKey(ch.dip, ch.udp.sport, ch.udp.dport, ch.udp.pg);

    ReunionTag tag;
    bool found=p->PeekPacketTag(tag);

    if(m_isToR){
        if(!found){
            ReunionFlowlet* flowlet=GetFlowlet(qpkey,ch,0);
            ReunionTag temp_tag;
            temp_tag.SetHopCount(0);
            temp_tag.SetPathId(flowlet->_PathId);
            p->AddPacketTag(temp_tag);
            return GetOutPortFromPath(flowlet->_PathId,0);
        }
        else{
            p->RemovePacketTag(tag);
            NS_LOG_DEBUG("ReceiverToR "
                        << m_switch_id
                        << " Path " << tag.GetPathId() << " "<<now);
            return REUNION_NULL;
        }
    }
    else{

        uint32_t hopCount=tag.GetHopCount()+1;
        tag.SetHopCount(hopCount);

        ReunionTag temp_tag;
        p->RemovePacketTag(temp_tag);
        p->AddPacketTag(tag);

        return GetOutPortFromPath(tag.GetPathId(),hopCount);
    }
    NS_ASSERT_MSG("false", "This should not be occured");
    };

    ReunionFlowlet* ReunionRouting::GetFlowlet(uint64_t Qpkey,CustomHeader ch,uint32_t hopcount){
        Time now = Simulator::Now();
        assert(Settings::hostIp2SwitchId.find(ch.dip) != Settings::hostIp2SwitchId.end());
        uint32_t dstToRId = Settings::hostIp2SwitchId[ch.dip];


        if(_QpkeyToFlowlet.find(Qpkey)!=_QpkeyToFlowlet.end()){
            ReunionFlowlet* flowlet=_QpkeyToFlowlet.find(Qpkey)->second;
            if(now-flowlet->_activeTime>_FlowletTimeout){
                flowlet->_lastReroute=now;
                flowlet->_PathId=GetRandomPath(dstToRId);
            }

            flowlet->_activeTime=now;
            if(now-flowlet->_lastReroute>_SubFlowletGap&&_HighUtilizedPort.find(GetOutPortFromPath(flowlet->_PathId,hopcount))!=_HighUtilizedPort.end()){
                auto pathItr = _ReunionRouteTable.find(dstToRId);
                assert(pathItr != _ReunionRouteTable.end());  // Cannot find dstToRId from ToLeafTable
                auto innerPathItr = pathItr->second.begin();
                while(innerPathItr!=pathItr->second.end()){
                    if(*innerPathItr!=flowlet->_PathId&&_LowUtilizedPort.find(GetOutPortFromPath(*innerPathItr,hopcount))!=_LowUtilizedPort.end()){
                        flowlet->_PathId=*innerPathItr;
                        _LowUtilizedPort.erase(_LowUtilizedPort.find(GetOutPortFromPath(*innerPathItr,hopcount)));
                        break;
                    }
                    innerPathItr++;
                }
                flowlet->_lastReroute=now;
            }
            return flowlet;
        }
        else{
            ReunionFlowlet* flowlet=new ReunionFlowlet;
            _QpkeyToFlowlet[Qpkey]=flowlet;
            flowlet->_activatedTime=now;
            flowlet->_activeTime=now;
            flowlet->_lastReroute=now;
            flowlet->_PathId=GetRandomPath(dstToRId);
            
            return flowlet;
        }
    }


    uint32_t ReunionRouting::GetRandomPath(uint32_t dstToRId) {
    NS_LOG_DEBUG("GetRandomPath: "<<m_switch_id<<" to "<<dstToRId);
    for(auto it=_ReunionRouteTable.begin();it!=_ReunionRouteTable.end();it++)
        NS_LOG_DEBUG("Routing table: "<<it->first<<" size: "<<it->second.size());
    auto pathItr = _ReunionRouteTable.find(dstToRId);
    assert(pathItr != _ReunionRouteTable.end());  // Cannot find dstToRId from ToLeafTable

    auto innerPathItr = pathItr->second.begin();
    std::advance(innerPathItr, rand() % pathItr->second.size());

    if(_PortTransmit.find(*innerPathItr)==_PortTransmit.end())
            _PortTransmit[*innerPathItr]=0;
    return *innerPathItr;
}

void ReunionRouting::AgingEvent(){

    Time now=Simulator::Now();
    auto itr=_QpkeyToFlowlet.begin();
    while(itr!=_QpkeyToFlowlet.end()){
        if(now-itr->second->_activeTime>_FlowletTimeout)
            itr=_QpkeyToFlowlet.erase(itr);
        else itr++;
    }

    _HighUtilizedPort.clear();
    _LowUtilizedPort.clear();

    for(auto portItr=_PortTransmit.begin();portItr!=_PortTransmit.end();portItr++){
        if(portItr->second>_HighUtilCount)_HighUtilizedPort.insert(portItr->first);
        if(portItr->second<_LowUtilCount)_LowUtilizedPort.insert(portItr->first);
        portItr->second=0;
    }

    m_agingEvent = Simulator::Schedule(m_agingTime, &ReunionRouting::AgingEvent, this);
}
}