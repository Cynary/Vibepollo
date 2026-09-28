#include "../../../src/nvenc/submission_trace.h"
#include <barrier>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
int main(int argc, char** argv) {
  assert(argc==2);
  const std::filesystem::path dir(argv[1]);
  std::filesystem::create_directories(dir);
  const auto prefix=(dir/"submit").string();
#ifdef _WIN32
  _putenv_s("MOONMACHINE_NVENC_SUBMIT_TRACE",prefix.c_str());
#else
  setenv("MOONMACHINE_NVENC_SUBMIT_TRACE",prefix.c_str(),1);
#endif
  std::barrier barrier(2);
  auto run=[&](int id){
    nvenc::submission_trace::recorder recorder;
    barrier.arrive_and_wait();
    const auto now=nvenc::submission_trace::clock::now();
    recorder.add(id,now,now,now);
  };
  std::thread a(run,1),b(run,2);a.join();b.join();
  int files=0;int sum=0;
  for(const auto& f:std::filesystem::directory_iterator(dir)){
    if(f.path().filename().string().find("submit.")!=0)continue;
    std::ifstream in(f.path());std::string header,row;std::getline(in,header);std::getline(in,row);
    assert(header=="frame,encode_entry_us,nvenc_submit_us,bitstream_ready_us");
    sum+=std::stoi(row);++files;
  }
  assert(files==2 && sum==3);
}
