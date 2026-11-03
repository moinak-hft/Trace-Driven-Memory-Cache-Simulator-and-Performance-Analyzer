#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif
// Pin can gate tracing between these markers. Both markers must survive -O3.
extern "C" NOINLINE void cachelab_roi_begin() {
#if defined(__GNUC__)
    asm volatile("" ::: "memory");
#endif
}
extern "C" NOINLINE void cachelab_roi_end() {
#if defined(__GNUC__)
    asm volatile("" ::: "memory");
#endif
}
static volatile double result_sink = 0;

int main(int argc,char** argv) {
    try {
        const std::string mode=argc>1?argv[1]:"matrix";
        const int n=argc>2?std::stoi(argv[2]):128;
        if(n<4 || n>4096) throw std::runtime_error("size must be 4..4096");
        if(mode!="matrix" && mode!="tiled" && mode!="pointer" && mode!="sort" && mode!="stream" && mode!="mixed")
            throw std::runtime_error("mode must be matrix|tiled|pointer|sort|stream|mixed");
        const std::size_t size=std::size_t(n)*n;
        std::vector<double> a(size),b(size),c(size,0);
        std::vector<std::size_t> next(size),order(size);
        std::mt19937 rng(42);
        for(std::size_t i=0;i<size;++i) { a[i]=double(i%97)/97; b[i]=double(i%89)/89; }
        std::iota(order.begin(),order.end(),0);
        std::shuffle(order.begin(),order.end(),rng);
        for(std::size_t i=0;i<size;++i) next[order[i]]=order[(i+1)%size];
        auto start=std::chrono::steady_clock::now();
        cachelab_roi_begin();
        double checksum=0;
        if(mode=="matrix") {
            for(int i=0;i<n;++i) for(int j=0;j<n;++j) for(int k=0;k<n;++k)
                c[std::size_t(i)*n+j]+=a[std::size_t(i)*n+k]*b[std::size_t(k)*n+j];
        } else if(mode=="tiled") {
            constexpr int tile=16;
            for(int ii=0;ii<n;ii+=tile) for(int kk=0;kk<n;kk+=tile) for(int jj=0;jj<n;jj+=tile)
                for(int i=ii;i<std::min(ii+tile,n);++i) for(int k=kk;k<std::min(kk+tile,n);++k)
                    for(int j=jj;j<std::min(jj+tile,n);++j)
                        c[std::size_t(i)*n+j]+=a[std::size_t(i)*n+k]*b[std::size_t(k)*n+j];
        } else if(mode=="pointer") {
            std::size_t node=order.front();
            for(std::size_t i=0;i<size*16;++i) { node=next[node]; checksum+=a[node]; }
        } else if(mode=="sort") {
            std::sort(a.begin(),a.end());
            checksum=a.front()+a.back();
        } else if(mode=="stream") {
            for(int repeat=0;repeat<8;++repeat) for(std::size_t i=0;i<size;++i) c[i]=a[i]+b[i];
        } else {
            for(int repeat=0;repeat<16;++repeat) for(std::size_t i=0;i<size;++i) {
                checksum+=a[i]; checksum+=b[(i%64)*8];
            }
        }
        cachelab_roi_end();
        auto finish=std::chrono::steady_clock::now();
        checksum+=std::accumulate(c.begin(),c.end(),0.0);
        result_sink=checksum;
        std::cout.precision(12);
        std::cout<<"workload="<<mode<<" n="<<n<<" checksum="<<checksum
                 <<" native_seconds="<<std::chrono::duration<double>(finish-start).count()<<'\n';
        return 0;
    } catch(const std::exception& e) { std::cerr<<"error: "<<e.what()<<'\n'; return 1; }
}
