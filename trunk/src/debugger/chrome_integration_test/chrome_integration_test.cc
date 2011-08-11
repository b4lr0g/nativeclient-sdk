// Copyright (c) 2011 The Native Client Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <time.h>
#include <windows.h>
#include <deque>
#include <string>
#include "debugger/base/debug_command_line.h"
#include "debugger/base/debug_socket.h"
#include "debugger/chrome_integration_test/process_utils.h"
#include "debugger/chrome_integration_test/rsp_registers.h"
#include "debugger/rsp/rsp_blob_utils.h"
#include "debugger/rsp/rsp_packet_utils.h"
#include "gtest/gtest.h"

#pragma warning(disable : 4996)  // Disable fopen warning.

namespace {
// specified from command line
// Fills from TARGET_HOST environment variable
std::string glb_target_host;

// Fills from TARGET_PORT environment variable
int glb_target_port = 0;

// Fills from NACL_SDK_ROOT environment variable
std::string glb_sdk_root;

// Fills from WEB_PORT environment variable
int glb_web_server_port = 0;

// Fills from ARCH_SIZE environment variable, 32 of 64
int glb_arch_size = 0;

// Fills from ONE_OP_TIMEOUT environment variable
int glb_wait_secs = 0;

// Fills from BROWSER environment variable
// Example:
//   BROWSER=D:\chrome-win32\chrome.exe --incognito http://localhost:5103/
// Note that these integration tests are running on examples/hello_world_c and
// examples/pi_generator nexes.
std::string glb_browser_cmd;

// Automatically formed based on NACL_SDK_ROOT and ARCH_SIZE
std::string glb_nexe_path;
std::string glb_objdump_path;
std::string glb_nacl_debugger_path;

const char* kSymbolsFileName = "symbols.txt";
const char* kDefaultTargetHost = "localhost";
const int kDefaultTargetPort = 4014;
const int kDefaultWebServerPort = 5103;
const int kDefaultArchSize = 64;
const int kDefaultOpTimeoutInSecs = 10;
const int kWaitForNexeSleepMs = 500;

const char* kNexePath =
    "\\src\\examples\\hello_world_c\\hello_world_x86_%d_dbg.nexe";

const char* kNexe2Path =
    "\\src\\examples\\pi_generator\\pi_generator_x86_%d_dbg.nexe";

const char* kObjdumpPath = "\\src\\toolchain\\win_x86\\bin\\nacl%s-objdump";
const char* kNaclDebuggerPath = "%sDebug\\nacl-gdb_server.exe";
const char* kWebServerPath = "\\src\\examples\\httpd.cmd";

std::string GetStringEnvVar(const std::string& name,
                            const std::string& default_value);
int GetIntEnvVar(const std::string& name, int default_value);
std::string DirFromPath(const std::string& command_line);
}  // namespace

int main(int argc, char* argv[]) {
  glb_sdk_root = GetStringEnvVar("NACL_SDK_ROOT", "");
  glb_target_host = GetStringEnvVar("TARGET_HOST", kDefaultTargetHost);
  glb_target_port = GetIntEnvVar("TARGET_PORT", kDefaultTargetPort);
  glb_arch_size = GetIntEnvVar("ARCH_SIZE", kDefaultArchSize);
  glb_wait_secs = GetIntEnvVar("ONE_OP_TIMEOUT", kDefaultOpTimeoutInSecs);
  glb_browser_cmd = GetStringEnvVar("BROWSER", "") +
      "hello_world_c/hello_world.html";
  glb_web_server_port = GetIntEnvVar("WEB_PORT", kDefaultWebServerPort);

  glb_nexe_path = rsp::Format(
      &debug::Blob(), kNexePath, glb_arch_size).ToString();
  glb_objdump_path = rsp::Format(
      &debug::Blob(),
      kObjdumpPath,
      (glb_arch_size == 64 ? "64" : "")).ToString();
  glb_nacl_debugger_path =
      rsp::Format(&debug::Blob(),
                  kNaclDebuggerPath,
                  (glb_arch_size == 64 ? "x64\\" : "")).ToString();

  // This function shall be called before we create any new process
  // or process_utils::KillProcessTree could kill innocent bystanders.
  process_utils::CreateListOfPreexistingProcesses();

  debug::CommandLine command_line;
  command_line = debug::CommandLine(argc, argv);

  // |repeat_number| specifies how many times unit tests run without breaking.
  // Used to detect flaky tests. Default is one.
  int repeat_number = command_line.GetIntSwitch("-repeat", 1);
  int res = 0;
  for (int i = 0; i < repeat_number; i++) {
    ::testing::InitGoogleTest(&argc, argv);
    res = RUN_ALL_TESTS();
    if (res != 0)
      break;
  }
  return res;
}

class NaclGdbServerTest : public ::testing::Test {
 public:
  NaclGdbServerTest();
  ~NaclGdbServerTest();

  void PostMsg(const std::string& msg);
  std::string ReceiveReply();
  std::string RPC(const std::string& request);
  std::string RPC(const debug::Blob& request);

  /// Starts web server specified by WEB_PORT environment variable,
  /// and waits till it accepts connections.
  /// @return error code, 0 for success
  int StartWebServer();

  /// Starts debugger with chrome sa debuggee.
  /// @return error code, 0 for success
  int StartDebugger(bool compatibility_mode);

  /// Connects to debugger RSP port.
  /// @return error code, 0 for success
  int ConnectToDebugger();

  /// Wait for debugged NaCl application to stop.
  /// Precondition: web server is up, debugger is up and connection
  /// to debugger is up.
  /// @return error code, 0 for success
  int WaitForNexeStart();

  /// Starts web server, debugger and waits for debugged
  /// NaCl application to stop.
  /// @return error code, 0 for success
  int InitAndWaitForNexeStart();

  /// Starts web server, debugger and waits for debugged
  /// NaCl application to stop, sets breakpoint at |Instance_DidCreate|,
  /// resumes application and waits for breakpoint to trigger.
  /// @return true for success
  bool InitAndRunToInstanceDidCreate();

  bool SetBreakpoint(uint64_t addr);
  bool DeleteBreakpoint();

  bool ReadMemory(uint64_t addr, int len, debug::Blob* data);
  bool WriteMemory(uint64_t addr, const debug::Blob& data);
  bool ReadRegisters(debug::Blob* gdb_regs);
  bool WriteRegisters(const debug::Blob& gdb_regs);
  bool ReadIP(uint64_t* ip);
  bool WriteIP(uint64_t ip);
  bool ReadInt32(uint64_t addr, uint32_t* dest);
  bool ReadInt64(uint64_t addr, uint64_t* dest);
  bool GetCurrThread(int* tid);
  bool GetThreadsList(std::deque<int>* tids);

  bool GetSymbolAddr(const std::string& name, uint64_t* addr);
  bool AddrToLineNumber(uint64_t addr, std::string* file_name, int* line_no);

  uint64_t GetMemoryBase();
  uint64_t TranslateIP(uint64_t ip);
  uint64_t TranslateDataAddr(uint64_t ip);
  uint64_t FlatAddrToIP(uint64_t ip);

 protected:
  bool RunObjdump(const std::string& nexe_path);

  HANDLE h_web_server_proc_;
  HANDLE h_debugger_proc_;
  debug::Socket nacl_gdb_server_conn_;
  uint8_t code_at_breakpoint_;
  uint64_t breakpoint_addr_;
  debug::RegistersSet regs_set_;
  uint64_t mem_base_;
  bool mem_base_requested_;
  bool objdump_runned_;
};

NaclGdbServerTest::NaclGdbServerTest()
    : h_web_server_proc_(NULL),
      h_debugger_proc_(NULL),
      code_at_breakpoint_(0),
      breakpoint_addr_(0),
      mem_base_(0),
      mem_base_requested_(false),
      objdump_runned_(false) {
  if (64 == glb_arch_size)
    regs_set_.InitializeForWin64();
  else
    regs_set_.InitializeForWin32();
}

NaclGdbServerTest::~NaclGdbServerTest() {
  if (NULL != h_web_server_proc_)
    process_utils::KillProcessTree(h_web_server_proc_);
  if (NULL != h_debugger_proc_)
    process_utils::KillProcessTree(h_debugger_proc_);
}

int NaclGdbServerTest::InitAndWaitForNexeStart() {
  int res = 0;
  if (0 != (res = StartWebServer()))
    return res;

  if (0 != (res = StartDebugger(false)))
    return res;

  if (0 != (res = ConnectToDebugger()))
    return res;

  if (0 != (res = WaitForNexeStart()))
    return res;
  return 0;
}

int NaclGdbServerTest::StartWebServer() {
  std::string web_server = glb_sdk_root + kWebServerPath;
  if (0 == web_server.size()) {
    return 1;
  }
  // start web server
  std::string path = DirFromPath(web_server);
  h_web_server_proc_ = process_utils::StartProcess(web_server, path.c_str());
  if (NULL == h_web_server_proc_)
    return 2;
  // wait for it to start up
  time_t end_ts = time(0) + glb_wait_secs;
  debug::Socket sock;
  do {
    if (sock.ConnectTo(glb_target_host, glb_web_server_port))
      break;
  } while (time(0) < end_ts);
  if (!sock.IsConnected())
    return 3;
  return 0;
}

int NaclGdbServerTest::StartDebugger(bool compatibility_mode) {
  std::string cmd = glb_nacl_debugger_path +
      (compatibility_mode ? " --cm" : "") +
      " --program \"" + glb_browser_cmd + "\"";
  h_debugger_proc_ = process_utils::StartProcess(cmd);
  if (NULL == h_debugger_proc_)
    return 12;
  return 0;
}

int NaclGdbServerTest::ConnectToDebugger() {
  time_t end_ts = time(0) + glb_wait_secs;
  do {
    if (nacl_gdb_server_conn_.ConnectTo(glb_target_host, glb_target_port))
      break;
  } while (time(0) < end_ts);
  if (!nacl_gdb_server_conn_.IsConnected()) {
    return 21;
  }
  return 0;
}

int NaclGdbServerTest::WaitForNexeStart() {
  time_t end_ts = time(0) + glb_wait_secs;
  do {
    std::string reply = RPC("?");
    if ("S13" == reply)
      return 0;
    Sleep(kWaitForNexeSleepMs);
  } while (time(0) < end_ts);
  return 35;
}

std::string NaclGdbServerTest::RPC(const std::string& request) {
  PostMsg(request);
  return ReceiveReply();
}

std::string NaclGdbServerTest::RPC(const debug::Blob& request) {
  return RPC(request.ToString());
}

void NaclGdbServerTest::PostMsg(const std::string& msg) {
  debug::Blob wire_req;
  rsp::PacketUtils::AddEnvelope(debug::Blob().FromString(msg), &wire_req);
  nacl_gdb_server_conn_.WriteAll(wire_req);
}

std::string NaclGdbServerTest::ReceiveReply() {
  const int kRpcTimeoutSecs = 10;
  debug::Blob rcv;
  debug::Blob wire_rcv;
  time_t end_ts = time(0) + kRpcTimeoutSecs;
  while (time(0) < end_ts) {
    char buff[100];
    size_t rd = nacl_gdb_server_conn_.Read(buff, sizeof(buff) - 1, 0);
    if (rd) {
      wire_rcv.Append(debug::Blob(buff, rd));
      if (rsp::PacketUtils::RemoveEnvelope(wire_rcv, &rcv)) {
        break;
      }
    }
  }
  return rcv.ToString();
}

bool NaclGdbServerTest::SetBreakpoint(uint64_t addr) {
  breakpoint_addr_ = addr;
  debug::Blob read_req;
  rsp::Format(&read_req, "m%I64x,1", addr);
  std::string reply = RPC(read_req);
  EXPECT_EQ(2 * sizeof(code_at_breakpoint_), reply.size());

  debug::Blob read_data;
  EXPECT_TRUE(read_data.FromHexString(reply));
  EXPECT_EQ(sizeof(code_at_breakpoint_), read_data.size());
  read_data.Peek(0, &code_at_breakpoint_, sizeof(code_at_breakpoint_));

  debug::Blob write_req;
  rsp::Format(&write_req, "M%I64x,1:cc", addr);
  reply = RPC(write_req);
  EXPECT_STREQ("OK", reply.c_str());
  return true;
}

bool NaclGdbServerTest::DeleteBreakpoint() {
  debug::Blob write_req;
  rsp::Format(&write_req, "M%I64x,1:%x", breakpoint_addr_, code_at_breakpoint_);
  std::string req = write_req.ToString();
  std::string reply = RPC(write_req);
  EXPECT_STREQ("OK", reply.c_str());
  return true;
}

bool NaclGdbServerTest::ReadRegisters(debug::Blob* gdb_regs) {
  if (NULL == gdb_regs)
    return false;
  std::string reply = RPC("g");
  return gdb_regs->FromHexString(reply);
}

bool NaclGdbServerTest::WriteRegisters(const debug::Blob& gdb_regs) {
  debug::Blob write_req;
  rsp::Format(&write_req, "G%s", gdb_regs.ToHexString().c_str());
  std::string reply = RPC(write_req);
  return (reply == "OK");
}

bool NaclGdbServerTest::ReadMemory(uint64_t addr, int len, debug::Blob* data) {
  if (NULL == data)
    return false;
  debug::Blob read_req;
  rsp::Format(&read_req, "m%I64x,%x", addr, len);
  std::string req = read_req.ToString();
  std::string reply = RPC(read_req);
  data->FromHexString(reply);
  return (data->size() == len);
}

bool NaclGdbServerTest::WriteMemory(uint64_t addr, const debug::Blob& data) {
  debug::Blob cmd;
  rsp::Format(&cmd,
              "M%I64x,%x:%s",
              addr,
              data.size(),
              data.ToHexString().c_str());
  std::string reply = RPC(cmd);
  return "OK" == reply;
}

bool NaclGdbServerTest::ReadIP(uint64_t* ip) {
  debug::Blob regs;
  if (!ReadRegisters(&regs))
    return false;
  return regs_set_.ReadRegisterFromGdbBlob(regs, "ip", ip);
}

bool NaclGdbServerTest::WriteIP(uint64_t ip) {
  debug::Blob regs;
  if (!ReadRegisters(&regs))
    return false;

  if (!regs_set_.WriteRegisterToGdbBlob("ip", ip, &regs))
    return false;

  return WriteRegisters(regs);
}

bool NaclGdbServerTest::ReadInt32(uint64_t addr, uint32_t* dest) {
  debug::Blob data;
  if (!ReadMemory(addr, sizeof(dest), &data))
    return false;

  data.Reverse();  // Integers are little-endian on x86.
  // Convert back to hex string, in order to |rsp::PopIntFromFront| work.
  data.FromString(data.ToHexString());
  return rsp::PopIntFromFront(&data, dest);
}

bool NaclGdbServerTest::ReadInt64(uint64_t addr, uint64_t* dest) {
  debug::Blob data;
  if (!ReadMemory(addr, sizeof(dest), &data))
    return false;

  data.Reverse();  // Integers are little-endian on x86.
  // Convert back to hex string, in order to |rsp::PopIntFromFront| work.
  data.FromString(data.ToHexString());
  return rsp::PopIntFromFront(&data, dest);
}

bool NaclGdbServerTest::GetCurrThread(int* tid) {
  std::string reply = RPC("qC");
  if (strcmp("QC", reply.substr(0, 2).c_str()) != 0)
    return false;
  return (1 == sscanf(reply.c_str() + 2, "%x", tid));
}

bool NaclGdbServerTest::GetThreadsList(std::deque<int>* tids) {
  tids->clear();
  /// Example: qfThreadInfo -> m1234,a34
  std::string reply = RPC("qfThreadInfo");
  if ((reply.size() == 0) || ('m' != reply[0]))
    return false;

  debug::Blob blob;
  blob.FromString(reply);
  blob.PopFront();
  std::deque<debug::Blob> tokens;
  blob.Split(debug::Blob().FromString(","), &tokens);
  for (size_t i = 0; i < tokens.size(); i++) {
    int tid = 0;
    if (rsp::PopIntFromFront(&tokens[i], &tid))
      tids->push_back(tid);
  }
  return true;
}

bool NaclGdbServerTest::RunObjdump(const std::string& nexe_path) {
  // Command line for this operation is something like this:
  // nacl64-objdump -t nacl_app.nexe > symbols.txt
  ::DeleteFile(kSymbolsFileName);
  std::string cmd = glb_sdk_root + glb_objdump_path + " -t " + nexe_path +
      " > " + kSymbolsFileName;
  int res = system(cmd.c_str());
  int attr = ::GetFileAttributes(kSymbolsFileName);
  return (-1 != res) && (INVALID_FILE_ATTRIBUTES != attr);
}

bool NaclGdbServerTest::GetSymbolAddr(const std::string& name,
                                      uint64_t* addr) {
  if (!objdump_runned_) {
    if (!RunObjdump(glb_sdk_root + glb_nexe_path))
      return false;
    objdump_runned_ = true;
  }

  FILE* file = fopen(kSymbolsFileName, "rt");
  if (NULL != file) {
    char line[MAX_PATH];
    while (fgets(line, sizeof(line) - 1, file)) {
      line[sizeof(line) - 1] = 0;
      debug::Blob blob;
      blob.FromString(line);
      rsp::RemoveSpacesFromBothEnds(&blob);
      std::deque<debug::Blob> tokens;
      // The line I am parsing looks something like this:
      // 0000000000029020 g    F .text  00000000000001a5 _start
      // Here, |_start| is the symbol I'm looking for, and
      // |0000000000029020| is the address.
      blob.Split(debug::Blob().FromString(" \n\r\t"), &tokens);
      if ((tokens.size() >= 2) &&
          (name == tokens[tokens.size() - 1].ToString())) {
        fclose(file);
        return rsp::PopIntFromFront(&tokens[0], addr);
      }
    }
    fclose(file);
  }
  return false;
}

bool NaclGdbServerTest::AddrToLineNumber(uint64_t addr,
                                         std::string* file_name,
                                         int* line_no) {
  if (NULL == line_no)
    return false;
  uint64_t ip = FlatAddrToIP(addr);

  *line_no = 0;
  const char* kLineFileName = "line.txt";
  char addr_str[200];
  _snprintf(addr_str, sizeof(addr_str), "0x%I64x", ip);
  std::string nexe_path = glb_sdk_root + glb_nexe_path;
  std::string cmd = glb_sdk_root + "\\src\\toolchain\\win_x86\\bin\\nacl" +
      (glb_arch_size == 64 ? "64" : "") + "-addr2line  --exe=" + nexe_path +
      " " + addr_str + " > " + kLineFileName;

  system(cmd.c_str());
  int scanned_items = 0;

  // line.txt shall have something like this:
  // /cygdrive/d/src/nacl_sdk2/src/examples/hello_world_c/hello_world.c:217
  FILE* file = fopen(kLineFileName, "rt");
  if (NULL != file) {
    char line[MAX_PATH] = {0};
    fgets(line, sizeof(line) - 1, file);
    line[sizeof(line) - 1] = 0;
    char src_file[MAX_PATH] = {0};
    scanned_items = sscanf(line, "%[^:]:%d", src_file, line_no);  // NOLINT
    *file_name = src_file;
    fclose(file);
  }
  return (2 == scanned_items);
}

uint64_t NaclGdbServerTest::TranslateIP(uint64_t ip) {
  if (64 == glb_arch_size)
    return ip + GetMemoryBase();
  return ip;
}

uint64_t NaclGdbServerTest::FlatAddrToIP(uint64_t ip) {
  if (64 == glb_arch_size)
    return ip - GetMemoryBase();
  return ip;
}

uint64_t NaclGdbServerTest::TranslateDataAddr(uint64_t addr) {
  if (64 == glb_arch_size)
    return addr;
  return addr + GetMemoryBase();
}

uint64_t NaclGdbServerTest::GetMemoryBase() {
  if (!mem_base_requested_) {
    std::string reply = RPC("qOffsets");
    // Expected reply: Text=c00000000;Data=c00000000

    debug::Blob blob;
    blob.FromString(reply);
    blob.PopBlobFromFrontUntilBytes(debug::Blob().FromString(";"));
    blob.PopBlobFromFrontUntilBytes(debug::Blob().FromString("="));
    mem_base_requested_ = rsp::PopIntFromFront(&blob, &mem_base_);
  }
  return mem_base_;
}

bool NaclGdbServerTest::InitAndRunToInstanceDidCreate() {
  if (0 != InitAndWaitForNexeStart())
    return false;

  uint64_t start_addr = 0;
  if (!GetSymbolAddr("Instance_DidCreate", &start_addr))
    return false;

  start_addr = TranslateIP(start_addr);

  if (!SetBreakpoint(start_addr))
    return false;
  if ("S05" != RPC("c"))
    return false;

  // Remove breakpoint, decrement ip.
  if (!DeleteBreakpoint())
    return false;

  uint64_t ip = 0;
  if (!ReadIP(&ip))
    return false;

  if ((start_addr + 1) != ip)
    return false;

  return WriteIP(ip - 1);
}

// Tests start here.
TEST_F(NaclGdbServerTest, StartWebServer) {
  EXPECT_EQ(0, StartWebServer());
}

TEST_F(NaclGdbServerTest, StartDebugger) {
  EXPECT_EQ(0, StartWebServer());
  EXPECT_EQ(0, StartDebugger(false));
}

TEST_F(NaclGdbServerTest, ConnectToDebugger) {
  EXPECT_EQ(0, StartWebServer());
  EXPECT_EQ(0, StartDebugger(false));
  EXPECT_EQ(0, ConnectToDebugger());
}

TEST_F(NaclGdbServerTest, WaitForNexeStart) {
  EXPECT_EQ(0, InitAndWaitForNexeStart());
}

TEST_F(NaclGdbServerTest, SetBreakpointAtEntryContinueAndHit) {
  ASSERT_EQ(0, InitAndWaitForNexeStart());

  uint64_t start_addr = 0;
  EXPECT_TRUE(GetSymbolAddr("_start", &start_addr));
  start_addr = TranslateIP(start_addr);

  EXPECT_TRUE(SetBreakpoint(start_addr));
  std::string reply = RPC("c");
  EXPECT_STREQ("S05", reply.c_str());

  uint64_t ip = 0;
  EXPECT_TRUE(ReadIP(&ip));
  EXPECT_EQ(start_addr + 1, ip);

  // read memory at IP, check it's a trap instruction
  debug::Blob data;
  EXPECT_TRUE(ReadMemory(start_addr, 1, &data));
  ASSERT_NE(0, data.size());
  EXPECT_EQ(0xcc, data[0]);

  // ip--
  EXPECT_TRUE(WriteIP(ip - 1));
  uint64_t new_ip = 0;
  EXPECT_TRUE(ReadIP(&new_ip));
  EXPECT_EQ(ip - 1, new_ip);

  // Delete breakpoint.
  EXPECT_TRUE(DeleteBreakpoint());
  data.Clear();
  EXPECT_TRUE(ReadMemory(start_addr, 1, &data));
  EXPECT_EQ(code_at_breakpoint_, data[0]);

  // Set breakpoint at |Instance_DidCreate|
  uint64_t instance_created_addr = 0;
  EXPECT_TRUE(GetSymbolAddr("Instance_DidCreate", &instance_created_addr));
  instance_created_addr = TranslateIP(instance_created_addr);
  EXPECT_TRUE(SetBreakpoint(instance_created_addr));

  // Nexe should be loaded and |Instance_DidCreate| called,
  // triggering breakpoint.
  EXPECT_STREQ("S05", RPC("c").c_str());

  ip = 0;
  EXPECT_TRUE(ReadIP(&ip));
  EXPECT_EQ(instance_created_addr + 1, ip);
}

TEST_F(NaclGdbServerTest, ThreadOps) {
  EXPECT_EQ(0, InitAndWaitForNexeStart());
  EXPECT_STREQ("OK", RPC("Hc-1").c_str());

  // Get current thread.
  int tid = 0;
  EXPECT_TRUE(GetCurrThread(&tid));
  EXPECT_NE(0, tid);

  // Is thread alive.
  debug::Blob cmd;
  rsp::Format(&cmd, "T%x", tid);
  EXPECT_STREQ("OK", RPC(cmd).c_str());

  rsp::Format(&cmd, "T%x", tid + 1);
  EXPECT_STREQ("E0b", RPC(cmd).c_str());

  // Set current thread.
  rsp::Format(&cmd, "Hc%x", tid);
  EXPECT_STREQ("OK", RPC(cmd).c_str());

  rsp::Format(&cmd, "Hc%x", tid + 1);
  EXPECT_STREQ("E0b", RPC(cmd).c_str());

  // Get thread list.
  std::deque<int> tids;
  EXPECT_TRUE(GetThreadsList(&tids));
  ASSERT_EQ(1, tids.size());
  EXPECT_EQ(tid, tids[0]);
}

// Inserts invalid opcode (instruction) at |Instance_DidCreate| address.
// Expects SIGILL signal reported.
TEST_F(NaclGdbServerTest, IllegalOpcode) {
  EXPECT_TRUE(InitAndRunToInstanceDidCreate());
  uint64_t instance_created_addr = 0;
  EXPECT_TRUE(ReadIP(&instance_created_addr));

  debug::Blob data;
  data.FromHexString("0f0b");
  EXPECT_TRUE(WriteMemory(instance_created_addr, data));

  EXPECT_STREQ("S04", RPC("c").c_str());  // SIGILL

  uint64_t ip = 0;
  EXPECT_TRUE(ReadIP(&ip));
  EXPECT_EQ(instance_created_addr, ip);
}

// Inserts opcodes for "*reinterpret_cast<char*>(0) = 1;" expression,
// which generates access violation exception (SIGSEGV).
TEST_F(NaclGdbServerTest, AccessViolation) {
  EXPECT_TRUE(InitAndRunToInstanceDidCreate());
  uint64_t instance_created_addr = 0;
  EXPECT_TRUE(ReadIP(&instance_created_addr));

  debug::Blob data;
  data.FromHexString("C6050000000001");  // *reinterpret_cast<char*>(0) = 1;
  EXPECT_TRUE(WriteMemory(instance_created_addr, data));

  EXPECT_STREQ("S0b", RPC("c").c_str());  // SIGSEGV

  uint64_t ip = 0;
  EXPECT_TRUE(ReadIP(&ip));
  EXPECT_EQ(instance_created_addr, ip);
}

TEST_F(NaclGdbServerTest, CompatibilityMode) {
  EXPECT_EQ(0, StartWebServer());
  EXPECT_EQ(0, StartDebugger(true));
  EXPECT_EQ(0, ConnectToDebugger());
  EXPECT_STREQ("S05", RPC("?").c_str());

  uint64_t start_addr = 0;
  EXPECT_TRUE(GetSymbolAddr("_start", &start_addr));
  start_addr = TranslateIP(start_addr);

  uint64_t ip = 0;
  EXPECT_TRUE(ReadIP(&ip));
  EXPECT_EQ(start_addr, ip);
}

TEST_F(NaclGdbServerTest, ReadStaticString) {
  static const char* kReverseTextMethodId = "reverseText";
  EXPECT_EQ(0, InitAndWaitForNexeStart());
  uint64_t str_ptr_addr = 0;
  EXPECT_TRUE(GetSymbolAddr("kReverseTextMethodId", &str_ptr_addr));
  str_ptr_addr = TranslateDataAddr(str_ptr_addr);

  // Pointer in nexe is 4 bytes long.
  uint32_t str_addr = 0;
  ASSERT_TRUE(ReadInt32(str_ptr_addr, &str_addr));
  uint64_t tr_str_addr = TranslateDataAddr(str_addr);

  size_t len = strlen(kReverseTextMethodId) + 1;
  debug::Blob str_data;
  EXPECT_TRUE(ReadMemory(tr_str_addr, len, &str_data));
  EXPECT_STREQ(kReverseTextMethodId, str_data.ToString().c_str());
}

TEST_F(NaclGdbServerTest, WriteStaticInt) {
  EXPECT_EQ(0, InitAndWaitForNexeStart());
  uint64_t int_var_addr = 0;
  EXPECT_TRUE(GetSymbolAddr("module_id", &int_var_addr));
  int_var_addr = TranslateDataAddr(int_var_addr);

  uint32_t var = 0;
  EXPECT_TRUE(ReadInt32(int_var_addr, &var));
  EXPECT_EQ(0, var);

  // Now, write something there.
  debug::Blob wr_data;
  wr_data.FromHexString("78563412");
  EXPECT_TRUE(WriteMemory(int_var_addr, wr_data));

  // And read it back.
  EXPECT_TRUE(ReadInt32(int_var_addr, &var));
  EXPECT_EQ(0x12345678, var);
}

TEST_F(NaclGdbServerTest, StepFromBreakpoint) {
  EXPECT_TRUE(InitAndRunToInstanceDidCreate());

  uint64_t ip = 0;
  EXPECT_TRUE(ReadIP(&ip));
  std::string file_name;
  int line1_no = 0;
  EXPECT_TRUE(AddrToLineNumber(ip, &file_name, &line1_no));

  // Single instruction step
  EXPECT_STREQ("S05", RPC("s").c_str());
  int line2_no = 0;
  EXPECT_TRUE(ReadIP(&ip));
  EXPECT_TRUE(AddrToLineNumber(ip, &file_name, &line2_no));

  // First step does not jump to different source line.
  EXPECT_EQ(line1_no, line2_no);

  // But performing several steps should jump to different source line.
  for (int i = 0; i < 20; i++) {
    EXPECT_STREQ("S05", RPC("s").c_str());
    EXPECT_TRUE(ReadIP(&ip));
    EXPECT_TRUE(AddrToLineNumber(ip, &file_name, &line2_no));
    EXPECT_NE(0, line2_no);
    if (line1_no != line2_no)
      break;
  }
  EXPECT_NE(line1_no, line2_no);
}

TEST_F(NaclGdbServerTest, ReadStackFrames) {
  EXPECT_TRUE(InitAndRunToInstanceDidCreate());
  debug::Blob regs;
  EXPECT_TRUE(ReadRegisters(&regs));

  // Reading stack pointer and base pointer
  uint64_t sp = 0;
  uint64_t bp = 0;
  EXPECT_TRUE(regs_set_.ReadRegisterFromGdbBlob(regs, "sp", &sp));
  EXPECT_TRUE(regs_set_.ReadRegisterFromGdbBlob(regs, "bp", &bp));
  EXPECT_GT(bp, sp);

  uint64_t prev_bp = 0;
  EXPECT_TRUE(ReadInt64(bp, &prev_bp));
  EXPECT_EQ(0, prev_bp);
}

TEST_F(NaclGdbServerTest, Multithreading) {
  glb_browser_cmd = GetStringEnvVar("BROWSER", "") +
      "pi_generator/pi_generator.html";
  glb_nexe_path = rsp::Format(
      &debug::Blob(), kNexe2Path, glb_arch_size).ToString();

  EXPECT_EQ(0, InitAndWaitForNexeStart());
  int main_tid = 0;
  EXPECT_TRUE(GetCurrThread(&main_tid));
  std::deque<int> tids;
  EXPECT_TRUE(GetThreadsList(&tids));
  ASSERT_EQ(1, tids.size());
  EXPECT_EQ(main_tid, tids[0]);

  // Now, resume nexe, it should stop when new thread is created.
  EXPECT_STREQ("S13", RPC("c").c_str());  // SIGSTOP

  // Set breakpoint at PiGenerator::ComputePi, wait for nexe to stop,
  // check thread list and current thread.
  // PiGenerator::ComputePi -> "_ZN12pi_generator11PiGenerator9ComputePiEPv"
  uint64_t compute_pi_addr = 0;
  EXPECT_TRUE(GetSymbolAddr("_ZN12pi_generator11PiGenerator9ComputePiEPv",
                            &compute_pi_addr));
  compute_pi_addr = TranslateIP(compute_pi_addr);
  EXPECT_TRUE(SetBreakpoint(compute_pi_addr));

  EXPECT_STREQ("S05", RPC("c").c_str());
  EXPECT_TRUE(DeleteBreakpoint());
  int background_tid = 0;
  EXPECT_TRUE(GetCurrThread(&background_tid));
  EXPECT_NE(main_tid, background_tid);
  EXPECT_TRUE(GetThreadsList(&tids));
  ASSERT_EQ(2, tids.size());
  EXPECT_EQ(main_tid, tids[0]);
  EXPECT_EQ(background_tid, tids[1]);

  // Set breakpoint at PiGenerator::Paint, wait for nexe to stop,
  // check thread list and current thread (should be main thread).
  // PiGenerator::Paint -> _ZN12pi_generator11PiGenerator5PaintEv
  uint64_t paint_addr = 0;
  EXPECT_TRUE(GetSymbolAddr("_ZN12pi_generator11PiGenerator5PaintEv",
                            &paint_addr));
  paint_addr = TranslateIP(paint_addr);
  EXPECT_TRUE(SetBreakpoint(paint_addr));

  EXPECT_STREQ("S05", RPC("c").c_str());
  EXPECT_TRUE(DeleteBreakpoint());
  int curr_tid = 0;
  EXPECT_TRUE(GetCurrThread(&curr_tid));
  EXPECT_EQ(main_tid, curr_tid);
}

namespace {
std::string GetStringEnvVar(const std::string& name,
                            const std::string& default_value) {
  char* value = getenv(name.c_str());
  if (NULL != value)
    return value;
  return default_value;
}

int GetIntEnvVar(const std::string& name,
                 int default_value) {
  char* value = getenv(name.c_str());
  if (NULL != value)
    return atoi(value);
  return default_value;
}

std::string DirFromPath(const std::string& command_line) {
  int last_dash_pos = 0;
  for (size_t i = 0; i < command_line.size(); i++)
    if (command_line[i] == '\\')
      last_dash_pos = i;

  std::string dir = command_line.substr(0, last_dash_pos);
  return dir;
}
}  // namespace
