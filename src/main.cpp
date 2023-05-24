#include <Arduino.h>
#include <ArduinoOTA.h>
#include <TinyConsole.h>
#include <TinyStreaming.h>
#include <TinyVim.h>
#include <LittleFS.h>
#include "auth.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <TinyVim.h>

#undef stdout
#undef stderr
#undef stdin

static tiny_vim::Window vim_term(0,0,0,0);
static tiny_vim::Splitter main_splitter(false, 20);
void resizeVimTerm()
{
  if (vim_term.top==0)
  {
    vim_term.top=1;
    vim_term.left=1;
    vim_term.width=Term.sx;
    vim_term.height=Term.sy;
  };
}

const TinyTerm::Color white=TinyTerm::white;
const TinyTerm::Color cyan=TinyTerm::cyan;

const std::string hostname="vimesp";

// put function declarations here:
int myFunction(int, int);

TinyVim* vim = nullptr;

void setupOta()
{
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPasswordHash("f64e51d7d8de34ef350a526467e0a610"); // ..5..

  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { });
  ArduinoOTA.onError([](ota_error_t error) {});

  ArduinoOTA.begin();
  WiFiClient t;
}

void setupWifi()
{
  Term << "init wifi...";
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
}

void trim(std::string& s)
{
  while(s.length() and s[0]==' ') s.erase(0,1);
}

std::string basename(std::string& file)
{
  auto pos=file.find_last_of('/');
  if (pos==std::string::npos) return file;
  return file.substr(pos+1);
}

int getInt(std::string& args)
{
  bool minus=false;
  if (args[0]=='-')
  {
    minus=true;
    args.erase(0,1);
  }
  int base=10;
  int (* fun)(int c) = isdigit;
  if (args.length()==0) return 0;
  if (args[0]=='0' and args[1]=='x')
  {
    fun=isxdigit;
    base=16;
    args.erase(0,2);
  };
  int r=0;
  while(args.length() and fun(args[0]))
  {
    int d;
    if (base==16)
    {
      d=(args[0] | 32);
      if (d>='a')
        d=d-'a'+10;
      else
        d=d-'0';
    }
    else
      d=args[0]-'0';

    r = r*base+d;
    args.erase(0,1);
  }
  trim(args);
  return minus ? -r : r;
}

std::string getWord(std::string& s, char sep=' ')
{
  trim(s);
  if (s.length()==0) return "";
  std::string r;
  if (s[0]=='"' or s[0]=='\'')
  {
    sep=s[0];
    s.erase(0,1);
  }
  auto pos=s.find(sep);
  if (pos == std::string::npos) pos=s.length();
  r=s.substr(0, pos);
  s.erase(0,pos+1);
  trim(s);
  return r;
}

char hdig(char c)
{
  if (c<10) return '0'+c;
  return 'A'+c-10;
}

std::string removeRedundantDots(std::string path)
{
    std::vector<std::string> dirs;
    std::string dir;
    std::string result;
    bool start = (!path.empty() && path[0] == '/');

    while (path.length())
    {
      dir=getWord(path, '/');
        if (dir == ".." && !dirs.empty())
          dirs.pop_back();
        else if (dir.length())
          dirs.push_back(dir);
    }
    for (const std::string& dir : dirs) {
      if (result.length()) result += '/';
        result += dir;
    }

    if (start) result = "/" + result;

    return result;
}

std::string getFile(const std::string& cwd, std::string &args)
{
  std::string name=getWord(args);
  if (name.length() == 0) return "";
  if (name[0]=='/') return name;
  if (cwd.length()==0) return '/'+name;
  if (cwd[cwd.length()-1]=='/') return cwd+name;
  return cwd+'/'+name;
}

Stream* stdout;
Stream* stderr;
Stream* stdin;

void onCommandInt(const std::string& s);

void onCommand(const std::string& s)
{
  Console << endl;
  onCommandInt(s);
  Console.prompt();
}

void forEachLine(const std::string name, std::function<void(const std::string&)> fun)
{
  char cr=0;
  std::string line;
  File f=LittleFS.open(name.c_str(), "r");
  if (not f)
  {
    Term << "error: cannot open file " << name << endl;
    return;
  }
  while(f.available())
  {
    char c=(char)f.read();
    if (c==cr or c==13 or c==10)
    {
      if (cr==0) { cr=c; }
      if (cr==c) fun(line);
      line.clear();
    }
    else
      line+=c;
  }
}

class CmdArg
{
  public:
    CmdArg(const char* cmd) : cmd_(cmd), args_(nullptr) {}
    CmdArg(const char* cmd, const char* args)
    : cmd_(cmd), args_(args) {}

    const char* cmd() { return cmd_; }
    const char* args() { return args_; }

    friend bool operator <(const CmdArg& l, const CmdArg& r)
    { return strcmp(l.cmd_, r.cmd_) < 0; }
    friend bool operator ==(const CmdArg& l, const CmdArg& r)
    { return strcmp(l.cmd_, r.cmd_)==0; }
    friend Stream& operator << (Stream& out, const CmdArg& l)
    { out << l.cmd_ << ' ' << (l.args_ ? l.args_ : ""); return out; }

  private:
    const char* cmd_;
    const char* args_;
};

int forEachFile(const std::string& cwd, std::string& args, std::function<bool(std::string& name)> fun)
{
  int count=0;
  while(args.length())
  {
    std::string name=getFile(cwd, args);
    if (LittleFS.exists(name.c_str()))
    {
      if (not fun(name)) break;
    }
    else
      *stdout << "File not found " << args << endl;  // TODO name of file
    count++;
  }
  return count;
}

void onCommandInt(const std::string& s)
{
  static std::string path="/bin:.";
  static std::string cwd="/";
  static std::string oldpwd;

  std::string s2 = s;
  trim(s2);
  if (s2.length()==0) return;
  std::string args;
  while(s2.length())
  {
    char q=s2[0];
    std::string arg=getWord(s2);
    if (arg==">" or arg==">>")
    {
      File redir = LittleFS.open(getFile(cwd, s2).c_str(), arg==">" ? "w" : "a");
      if (redir)
      {
        Stream* old=stdout;
        stdout=&redir;
        onCommandInt(args);
        stdout=old;
        *stdout << endl;
        return;
      }
    }
    else
    {
      if (args.length()) args+=' ';
      if (arg.find(' ')!=std::string::npos)
        args += q + arg + q;
      else
        args += arg;
    }
  }

  static std::map<CmdArg, std::function<void(std::string&)>> commands =
  {
    { {"int", "expr"}, [](std::string& args) {
      Term << args << '=';
      long l = getInt(args);
      Term << l << ", " << hex(l) << endl;
    }},
    { {"print_at", "x y str"}, [](std::string& args)
    {
      Term.saveCursor();
      Term.gotoxy(getInt(args), getInt(args));
      Term << args;
      Term.restoreCursor();
    }
    },
    { {"s", "help"}, [](std::string& args) {    // Testing Vim splitters
      resizeVimTerm();
      std::string cmd=getWord(args);
      std::string arg;

      if (cmd=="?" or cmd=="help")
      {
        Term << "main" << endl;
        Term << "draw" << endl;
        Term << "calc wid" << endl;
        Term << "dump / dump2" << endl;
        Term << "split wid v|h size 0|1 (side)" << endl;
        Term << "show wid" << endl;
        Term << "term [dx dy [w h]]" << endl;
      }
      else if (cmd == "draw")
      {
        main_splitter.draw(vim_term);
        Term << endl;
      }
      else if (cmd == "term")
      {
        if (args.length())
        {
          int16_t dx = getInt(args);
          int16_t dy = getInt(args);
          if (args.length())
          {
            int16_t w = getInt(args);
            int16_t h = getInt(args);
            if (w && h)
              vim_term = tiny_vim::Window(dy, dx, w, h);
            else
              *stdout << "invalid" << endl;
          }
          else
          {
            vim_term.left += dx;
            vim_term.top += dy;
            vim_term.width -= 2*dx;
            vim_term.height -= 2*dy;
          }
        }
        vim_term.frame(Term);
        *stdout << vim_term << endl;
      }
      else if (cmd == "calc")
      {
       uint16_t wid=getInt(args);
        if (wid)
        {
          Term << "calc " << hex(wid) << " from " << vim_term << endl;
          bool result = main_splitter.calcWindow(wid, vim_term);
          Term << (result ? "ok " : "ko ") << vim_term << endl;
        }
        else
          Term << "bad wid" << endl;
      }
      else if (cmd == "split")
      {
        uint16_t wid = getInt(args);
        std::string dir=getWord(args);
        uint16_t size=getInt(args);
        if (isdigit(args[0]) and size and (dir == "v" or dir == "h"))
          main_splitter.split(wid, vim_term, dir[0]=='v', getInt(args), size);
        else
          Term << "split error in args " << args << endl;
      }
      else if (cmd=="dump") main_splitter.dump(vim_term);
      else if (cmd=="dump2") main_splitter.dump2(vim_term);
      else *stdout << "Unknown command (" << cmd << ")" << endl;
    }},
    { "reset", [](std::string& args) { ESP.reset(); }},
    { "echo", [](std::string& args) {
        while(args.length())
        {
          *stdout << getWord(args);
          if (args.length()) *stdout << ' ';
        }
        *stdout << endl;
      }},
    { "path", [](std::string& args) {
      if (args.length())
        path=getWord(args);
      else
        *stdout << path << endl;
    }},
    { { "vim", "files"}, [](std::string& args) {
      if (vim==nullptr)
      {
        std::string files=""; // TODO list<string> should be better
        forEachFile(cwd, args, [&files](const std::string& name)->bool
        {
          if (files.length()) files +=' ';
          files+=name;
          return true;
        });
        vim = new TinyVim(&Term, files);
        *stdout << "vim (" << files << ")" << endl;
      }
    }},
    { { "head", "+n|-n file"}, [](std::string& args) {
      int rows=getInt(args);
      forEachLine(getFile(cwd, args), [&rows](const std::string& line)
      { if (rows++<0) *stdout << line << endl; });
    }},
    { { "nl", "file"}, [](std::string& args)
    {
      int row=1;
      forEachLine(getFile(cwd, args), [&row](const std::string& line)
      {
        *stdout << (row<10 ? "  " : ( row<100 ? " " : ""));
        *stdout << row++ << ' ' << line << endl;
      });
    }},
    { "clear", [](std::string& args) { Term.clear(); }},
    { { "tail", "+n|-n file"}, [](std::string& args) {
      int from=0;
      if (args[0]=='+')
        args.erase(0,1);
      int rows=-getInt(args);
      if (rows>0)
      {
        std::list<std::string> tail;
        forEachLine(getFile(cwd, args), [&rows,&tail](const std::string& line)
        {
          if (tail.size() >= (size_t)rows) tail.pop_front();
          tail.push_back(line);
        });
        for(const auto& s: tail) *stdout << s << endl;
      }
      else
      {
        from=-rows;
        rows=1;
        forEachLine(getFile(cwd, args), [&rows,&from](const std::string& line)
        {
          if (rows++>=from) *stdout << line << endl;
        });
      }
    }},
    { "df", [](std::string&) {
      FSInfo infos;
      LittleFS.info(infos);
      *stdout << infos.usedBytes << " bytes on " <<  infos.totalBytes
        << ", free: " << infos.totalBytes-infos.usedBytes << endl;
    }},
    { { "mv", "file file"}, [](std::string& args) {
      std::string source=getFile(cwd, args);
      std::string dest=getFile(cwd, args);
      auto dir=LittleFS.open(dest.c_str(), "r");
      if (dir.isDirectory()) dest += '/' + basename(source);
      dir.close();
      if (dest.length() and LittleFS.rename(source.c_str(), dest.c_str()))
        return;
      else
        *stdout << "error mv " << source << ' ' << dest << endl;
    }},
    { { "ls", "[files]"} , [](std::string& args) {
        if (args.length()==0) args=cwd;
        forEachFile(cwd, args, [](const std::string& name)->bool
        {
          if (LittleFS.exists(name.c_str()))
          {
            std::map<String, std::string> files;
            Dir dir = LittleFS.openDir(name.c_str());
            if (stdout==&Term) Term << cyan;
            while (dir.next())
            {
              std::string size=std::to_string(dir.fileSize());
              while(size.length()<6) size=' '+size;
              if (dir.isDirectory())
                *stdout << size << ' ' << dir.fileName() << '/' << endl;
              else
                files[dir.fileName()] = size;
            }
            if (stdout==&Term) Term << white;
            for(auto file: files)
            {
              *stdout << file.second << ' ';
              if (file.first[file.first.length()-1]==' ')
                *stdout << '\'' << file.first << '\'' << endl;
              else
                *stdout << file.first << endl;
            }
          }
          return true;
        });
      }
    },
    /* WGET HTTPS
    { "wget", [](std::string& args) {
      if (args.length()==0) return;
      std::string output=cwd+'/'+basename(args);
      Console << "output file: " << output << '.' << endl;
      File out = LittleFS.open(output.c_str(), "w");
      BearSSL::WiFiClientSecure client;
      client.setInsecure();
      HTTPClient https;
      if (out and https.begin(client, args.c_str()))
      {
        int rep = https.GET();
        if (rep>0)
        {
          String payload = https.getString();
          out.write(payload.c_str());
        }
        else
          *stderr << "error " << rep << endl;
      }
      else if (out)
        *stderr << "Unable to connect" << endl;
    }},
        */
    { { "wc" , "files"}, [](std::string& args) {
      forEachFile(cwd, args, [](const std::string& name) -> bool
      {
        File file=LittleFS.open(name.c_str(), "r");
        int cr=0;
        bool word=false;
        int lines=0; int words=0; int chars=0;
        if (file)
        {
          while(file.available())
          {
            chars++;
            char c=(char)file.read();
            if (c==cr or c==13 or c==10)
            {
              if (cr==0) cr=c;
              if (word) { words++; word=false;
              }
              if (cr==c) lines++;
            }
            else if (c==' ')
            { if (word) { words++; word=false; } }
            else
              word=true;
          }
          *stdout << lines << ' ' << words << ' ' << chars << ' ' << name << endl;
        }
        else
          *stderr << '?' << name << endl;
        return true;
      });
    }},
    { { "cd", "dir"}, [](std::string& args) {
      std::string dir;
      if (args=="-")
        dir=oldpwd;
      else
        dir=getFile(cwd,args);
      oldpwd=cwd;
      if (dir.length()==0)
      {
        cwd="/";
        return;
      }
      if (dir[dir.length()-1] != '/') dir += '/';
      if (LittleFS.exists(dir.c_str()))
        cwd=removeRedundantDots(dir);
      else
        *stderr << "not found:" << dir << endl;
    }},
    { { "hexdump", "file"}, [](std::string& args) {
      forEachFile(cwd, args, [](const std::string& name)->bool
      {
        File f=LittleFS.open(name.c_str(),"r");
        if (f)
        {
          int count=0;
          *stdout << name << endl;
          std::string ascii;
          while(f.available())
          {
            char c=f.read();
            if ((count%2)==0) *stdout << ' ';
            if ((count%16)==0)
            {
              *stdout << ' ' << ascii << endl;
              ascii.clear();
            }
            *stdout << hdig(c>>4) << hdig(c&0xF);
            if (c>=32 and c<=128)
              ascii+=c;
            else
              ascii+='.';
            count++;
          }
          while(count%16)
          {
            if (count%2==0) *stdout << ' ';
            *stdout << "  ";
            count++;
          }
          *stdout << ' ' << ascii << endl << endl;
        }
        return true;
      });
    }},
    { { "cat", "files" }, [](std::string& args) {
        forEachFile(cwd, args, [](const std::string& name)->bool
        {
          forEachLine(name, [](const std::string& line)
          {
            *stdout << line << endl;
          });
          return true;
        });
    }},
    { "pwd", [](std::string& args) { *stdout << cwd << endl; }},
    { "free", [](std::string& args) {
      *stdout << "Stack size " << CONFIG_ARDUINO_LOOP_STACK_SIZE << endl;
      *stdout << "m: free mem " << system_get_free_heap_size() << endl;
    }},
    { { "ansi", "str"}, [](std::string& args){
      static const char* CSI="\033[";
      Term << CSI << args;
    }},
    { "termcap", [](std::string& args) {
      *stdout << "term " << (Term.isTerm() ? "yes" : "no") << ", csi=" << Term.csi << endl;
      *stdout << "size=" << Term.sx << 'x' << Term.sy << endl;
      if (args!="")
      {
        *stdout << "Sending ask termsize" << endl;
        Term.getTermSize();
      }
    }},
    { "help", [](std::string& args) {
        *stdout << "List of commands: ";

        for(auto cmd: commands)
        { *stdout << "  "; *stdout << cmd.first << endl; }
        *stdout << endl;
    }},
    { {"touch", "file"}, [](std::string& args) {
      while(args.length())
      {
        std::string name=getFile(cwd,args);
        File file = LittleFS.open(name.c_str(), "w+");
        if (not file) *stderr << "Error touching " << name << endl;
        file.close();
      }
    }},
    { { "append", "file str"}, [](std::string& args){
      std::string name=getFile(cwd, args);
      if (name.length()==0) return;
      File f=LittleFS.open(name.c_str(),"a");
      if (f)
      {
        args+="\n\r";
        f.write(args.c_str());
      }
      else
        *stderr << "error" << endl;
    }},
    { {"create", "file rows str"}, [](std::string& args){
      std::string name=getFile(cwd, args);
      if (name.length()==0) return;
      File f=LittleFS.open(name.c_str(),"w");
      if (f)
      {
        int t=atol(args.c_str());
        getWord(args);
        args+="\n\r";
        while(t--)
          f.write(args.c_str());
      }
      else
        *stderr << "error creating " << name << endl;
    }},
    { { "mkdir", "dir" }, [](std::string& args) {
      while(args.length())
      {
        std::string name=getFile(cwd, args);
        if (not LittleFS.mkdir(name.c_str())) *stderr << "error" << endl;
      }
    }},
    { {"rmdir", "dir"}, [](std::string& args) {
      while(args.length())
      {
        std::string name=getFile(cwd, args);
        if (not LittleFS.rmdir(name.c_str())) *stderr << "error " << name << endl;
      }
    }},
    { {"rm", "file"}, [](std::string& args) {
      while(args.length())
      {
        std::string name=getFile(cwd, args);
        LittleFS.remove(name.c_str());
      }
    }},
    { "format", [](std::string& args) { LittleFS.format(); }},
  };
  std::string cmd = getWord(args);

  bool found = false;
  for(auto it: commands)
  {
    if (it.first == cmd.c_str())
    {
      found = true;
      it.second(args);
    }
  }
  if (not found)
  {
    std::string paths=path;
    std::string name = getWord(cmd);
    if (name.substr(0,2)=="./") name=cwd+'/'+name.substr(2);
    while(paths.length())
    {
      std::string p=getWord(paths, ':');
      std::string path_name;
      if (name[0]=='/')
        path_name=name;
      else if (p[0]=='/')
        path_name=p+'/'+name;
      else
        path_name=cwd+'/'+p+'/'+name;
      if (LittleFS.exists(path_name.c_str()))
      {
        found=true;
        File file = LittleFS.open(path_name.c_str(), "r");
        cmd.clear();
        while (file.available())
        {
          char c = (char)file.read();
          if (c == 13 or c == 10)
          {
            if (cmd.length())
            {
              if (args.length()) args=' '+args;
              onCommandInt(cmd);
            }
            cmd.clear();
          }
          else
            cmd += c;
        }
        break;
      }
    }
    if (not found)
      *stderr << "Command not found '" << name << "'" << endl;
  }
  Console.setPrompt((cwd.substr(1)+"> ").c_str());
}

void onMouse(const TinyTerm::MouseEvent& evt)
{
  resizeVimTerm();

  tiny_vim::Coord coords(evt.y, evt.x);
  tiny_vim::Window tmp_term(vim_term);
  auto wid = main_splitter.findWindow(tmp_term, coords);
  Term.saveCursor();
  Term.gotoxy(1, Term.sx/3);
  Term << "mouse " << hex(evt.value) << ' ' << coords
    << ", vim_term:" << hex(wid) << ' ' << tmp_term << "    " << endl;
  Term.restoreCursor();
}

void setup() {
  Serial.begin(115200);
  Console.begin(Serial);
  setupWifi();
  setupOta();
  Console.setCallback(onCommand);
  Term.onMouse(onMouse);
  if (!LittleFS.begin())
  {
    Console << "Unable to mount fs" << endl;
  }
  stdout=&Term;
  stderr=&Term;
  stdin=&Term;
  onCommand("/etc/shrc");
}

void loop() {
  static bool connected=false;
  if (not connected and WiFi.waitForConnectResult() == WL_CONNECTED)
  {
    Term << "Wifi ok" << endl;
    connected = true;
    Console.prompt();
  }
  ArduinoOTA.handle();
  Term.loop();
  if (vim)
  {
    vim->loop();
    if (vim->state() == TinyApp::ENDED)
    {
      delete vim;
      vim=nullptr;
      Term << "vim ended" << endl;
      Console.prompt();
    }
  }
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}