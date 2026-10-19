// Uses the repository module parser and sampler, including float32
// quaternion normalization on load, shortest arc/nlerp cutoff and std::lerp.
#include "accessory_table.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace BetterEndfieldNext::Actions;

AccessoryTable load(const char* path) {
    AccessoryTable t; std::string err; std::ifstream f(path, std::ios::binary);
    if (!f || !t.Load(f, err)) throw std::runtime_error(std::string(path) + ": " + err);
    return t;
}
void word(std::ostream& o, uint32_t v, unsigned n=4) {
    for (unsigned i=0;i<n;++i) o.put(char(v>>(8*i)));
}
void real(std::ostream& o, float v) { word(o,std::bit_cast<uint32_t>(v)); }
void pose(std::ostream& o, const BonePose& p) {
    for (float v : {p.rotation.x,p.rotation.y,p.rotation.z,p.rotation.w,
                    p.position.x,p.position.y,p.position.z}) real(o,v);
}
double rotation_error(PoseQuaternion a, PoseQuaternion b) {
    double x[4]={a.x,a.y,a.z,a.w}, y[4]={b.x,b.y,b.z,b.w};
    double nx=0,ny=0,dot=0;
    for(int i=0;i<4;++i){nx+=x[i]*x[i];ny+=y[i]*y[i];dot+=x[i]*y[i];}
    nx=std::sqrt(nx);ny=std::sqrt(ny);
    double minus=0,plus=0;
    for(int i=0;i<4;++i){double u=x[i]/nx,v=y[i]/ny*(dot<0?-1:1);minus+=(u-v)*(u-v);plus+=(u+v)*(u+v);}
    return 4*std::atan2(std::sqrt(minus),std::sqrt(plus))*180/std::acos(-1.);
}
struct Metrics {
    double deg=0,cm=0,deg_phase=0,cm_phase=0; std::string deg_bone,cm_bone;
    void add(double d,double p,double ph,const std::string& name){
        if(deg_bone.empty()||d>deg){deg=d;deg_phase=ph;deg_bone=name;}
        if(cm_bone.empty()||p>cm){cm=p;cm_phase=ph;cm_bone=name;}
    }
    void print()const{
        std::cout<<"{\"rotation_deg\":"<<deg<<",\"position_cm\":"<<cm
          <<",\"rotation_phase\":"<<deg_phase<<",\"position_phase\":"<<cm_phase
          <<",\"rotation_bone\":"<<std::quoted(deg_bone)<<",\"position_bone\":"<<std::quoted(cm_bone)<<"}";
    }
};
int main(int argc,char** argv) { try {
    if(argc<3) throw std::runtime_error("inspect SOURCE | resample SOURCE DEST GROUPS N | compare REFERENCE CANDIDATE");
    const auto reference=load(argv[2]);
    if(std::string(argv[1])=="inspect") {
        if(argc!=3) throw std::runtime_error("inspect SOURCE");
        std::cout<<std::setprecision(9)<<"BEACC001 version=1 entries="<<reference.entries.size()<<'\n';
        for(const auto& e:reference.entries)
            std::cout<<"state="<<e.state_hash<<" begin="<<e.begin<<" target="<<e.target
                     <<" N="<<e.sample_count<<" M="<<e.bones.size()<<'\n';
    } else if(std::string(argv[1])=="resample") {
        if(argc!=6) throw std::runtime_error("resample SOURCE DEST GROUPS N");
        unsigned n=std::stoul(argv[5]);if(n<8||n>4096)throw std::runtime_error("N outside 8..4096");
        std::set<unsigned> groups;std::istringstream gs(argv[4]);std::string token;
        while(std::getline(gs,token,','))groups.insert(std::stoul(token));
        std::ofstream o(argv[3],std::ios::binary);if(!o)throw std::runtime_error("output open failed");
        o.write("BEACC001",8);word(o,1);word(o,reference.entries.size());
        for(const auto& e:reference.entries){
            std::vector<size_t> keep;for(size_t b=0;b<e.bones.size();++b)if(groups.contains(e.bones[b].group))keep.push_back(b);
            if(keep.empty())throw std::runtime_error("no retained bones");
            word(o,std::bit_cast<uint32_t>(e.state_hash));real(o,e.begin);real(o,e.target);word(o,n);word(o,keep.size());
            for(auto b:keep){const auto& bone=e.bones[b];word(o,bone.group,1);word(o,bone.name.size(),2);o<<bone.name;}
            for(unsigned j=0;j<n;++j)for(auto b:keep){BonePose p;if(!e.Sample(b,double(j)/n,p))throw std::runtime_error("Sample failed");pose(o,p);}
        }
        if(!o)throw std::runtime_error("output write failed");
    } else if(std::string(argv[1])=="compare") {
        if(argc!=4) throw std::runtime_error("compare REFERENCE CANDIDATE");
        const auto candidate=load(argv[3]);
        if(reference.entries.size()!=candidate.entries.size())throw std::runtime_error("entry count mismatch");
        bool pass=true;std::cout<<std::setprecision(17)<<"{\"grid_phases\":8192,\"entries\":[";bool first=true;
        for(const auto& c:candidate.entries){
            auto it=std::find_if(reference.entries.begin(),reference.entries.end(),[&](const auto& e){return e.state_hash==c.state_hash;});
            if(it==reference.entries.end())throw std::runtime_error("missing state");const auto& r=*it;
            if(r.begin!=c.begin||r.target!=c.target)throw std::runtime_error("window mismatch");
            std::map<unsigned,Metrics> stats;std::map<std::string,Metrics> bones;
            for(size_t b=0;b<c.bones.size();++b){const auto& bone=c.bones[b];
                auto rb=std::find_if(r.bones.begin(),r.bones.end(),[&](const auto& v){return v.name==bone.name&&v.group==bone.group;});
                if(rb==r.bones.end())throw std::runtime_error("missing bone");size_t idx=rb-r.bones.begin();
                for(unsigned j=0;j<8192;++j){double ph=double(j)/8192;BonePose a,z;
                    if(!r.Sample(idx,ph,a)||!c.Sample(b,ph,z))throw std::runtime_error("Sample failed");
                    double d=rotation_error(a.rotation,z.rotation);
                    double dx=double(a.position.x)-z.position.x,dy=double(a.position.y)-z.position.y,dz=double(a.position.z)-z.position.z;
                    double p=100*std::sqrt(dx*dx+dy*dy+dz*dz);
                    stats[bone.group].add(d,p,ph,bone.name);bones[bone.name].add(d,p,ph,bone.name);
                    if(d>.05||p>.01)pass=false;
                }
            }
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"state_hash\":"<<c.state_hash<<",\"N\":"<<c.sample_count<<",\"M\":"<<c.bones.size()<<",\"groups\":{";
            bool fg=true;for(const auto& [g,m]:stats){if(!fg)std::cout<<',';fg=false;std::cout<<std::quoted(std::to_string(g))<<':';m.print();}
            std::cout<<"},\"bones\":{";bool fb=true;for(const auto& [name,m]:bones){if(!fb)std::cout<<',';fb=false;std::cout<<std::quoted(name)<<':';m.print();}
            std::cout<<"}}";
        }
        std::cout<<"],\"passed\":"<<(pass?"true":"false")<<"}\n";
    } else throw std::runtime_error("unknown command");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
