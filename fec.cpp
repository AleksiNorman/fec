#include "config.h"

#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <dirent.h>

#include <iostream>
#include <vector>
#include <utility>
#include <locale>
#include <algorithm>

#ifndef HAVE_STRCASECMP
int strcasecmp(const char* s1, const char* s2);
#endif

#ifndef HAVE_STRDUP
char* strdup(const char* s1);
#endif

#ifndef HAVE_STRRCHR
char* strrchr*(const char* s1, char c1);
#endif

static struct {
  bool	recursive;
  bool	case_insensitive;
  bool	human_readable;
  bool	count_links;

  int	verbosity;
  
#ifdef HAVE_LSTAT
  bool	dereference;
#endif
} globaloptions;

#define LOG(level, x) if (level <= globaloptions.verbosity) { std::cerr << x;}

void cerrlog(int level, char* output)
{
  if (level >= globaloptions.verbosity)
    std::cerr << output;
}

namespace type {

  // used in a vector (must be assignable)
  class device {
    public:
      typedef typename std::pair<ino_t, off_t>	atom_t;
      typedef typename std::vector<atom_t>	table_t;
      typedef typename std::vector< std::pair<ino_t, off_t> >::iterator	iterator;
      
      // standard constuctors
      device() : metadata(), dev_id(0) {}
      device(dev_t st_dev) : metadata(), dev_id(st_dev) {}
      device(const device& rhs) : metadata(rhs.metadata), dev_id(rhs.dev_id) {}

      // standard assignment
      const device& operator=(const device& rhs)
      {
	assign(rhs);
	return *this;
      }

      void assign(const device& rhs)
      {
	if (&rhs != this) {
	  metadata = rhs.metadata;
	  dev_id = rhs.dev_id;
	}
      }
      
      // assignment from dev_t
      void assign(dev_t st_dev)
      {
	metadata.clear();
	dev_id = st_dev;
      }
      
      void add(struct stat& st)
      {	metadata.push_back( atom_t(st.st_ino, st.st_size) ); }
      
      dev_t getdevice() const
      { return dev_id; }
      
      unsigned int getcount() const
      {	return metadata.size(); }
      
      off_t getbytes() const
      { 
	off_t ret = 0;
	std::vector< std::pair<ino_t, off_t> >::const_iterator i;
	for (i = metadata.begin(); i != metadata.end(); i++)
	  ret += i->second;
	return ret;
      }
      
      void remove_duplicates()
      { 
	metadata.erase(std::unique(metadata.begin(), metadata.end()), metadata.end());
      }
      
    protected:
      table_t metadata;
      dev_t dev_id;
    
  };
      

  
  class entry {
    public:
      // standard constuctors
      entry() : id(0), devices() {}
      /*
      entry(const entry& rhs) : id(0), count(rhs.count), bytes(rhs.bytes)
      { setid( rhs.getid() ); }
      */

      ~entry()
      { if (id) free(id); }
      
      // convinience constructor
      explicit entry(const char* fileid, struct stat& st) : 
	id(0), devices()
      { 
	assign(fileid, st);
      }
      
      // standard assignment
      /*
      const entry& operator=(const entry& rhs)
      { 
	assign(rhs);
	return *this;
      }
      
      void assign(const entry& rhs)
      {
	setid(rhs.id);
	devices = rhs.devices;
      }
      */
      
      // assignment from stat (creates a new fileid)
      void assign(const char* fileid, struct stat& st)
      {
	setid(fileid);
	add(st);
      }
      
      // add to existing fileid
      void add(struct stat& st)
      {
	std::vector<device>::reverse_iterator riter;
	for (riter = devices.rbegin(); riter != devices.rend(); riter++)
	  if ( st.st_dev == riter->getdevice() )
	    break;
	  
	if (riter == devices.rend()) {
	  devices.push_back( device(st.st_dev) );
	  devices.rbegin()->add(st);
	} else {
	  riter->add(st);
	}
      }
      
      void setid(const char* ptr)
      { resetid(); copyid(ptr); }

      char* getid() const
      { return id;}
      
      unsigned int getdevices() const
      { return devices.size(); }
      
      unsigned int getcount() const
      { 
	unsigned int count = 0;
	std::vector<device>::const_iterator iter;
	for (iter = devices.begin(); iter != devices.end(); iter++)
	  count += iter->getcount();
	return count;
      }
      
      off_t getbytes() const
      {
	off_t bytes = 0;
	std::vector<device>::const_iterator iter;
	for (iter = devices.begin(); iter != devices.end(); iter++)
	  bytes += iter->getbytes();
	return bytes;
      }

      
      void remove_duplicates()
      {
	std::vector<device>::iterator iter;
	for (iter = devices.begin(); iter != devices.end(); iter++)
	  iter->remove_duplicates();
      }
      
    protected:
      char* id;
      std::vector<device> devices;
      
    private:
      void resetid()
      { 
	if (id) {
	  free(id);
	  id = 0;
	}
      }    
      
      void copyid(const char* ptr)
      { id = strdup(ptr); }
  };
  
  class classifier {
    public:
      virtual bool operator()(const char*, const char*) = 0;
  };
  
  class binary_classifier : public classifier {
    public:
      bool operator()(const char* s1, const char* s2)
      { return strcmp(s1, s2) == 0; }
  };

  
  class case_insensitive_classifier : public classifier {
    public:
      bool operator()(const char* s1, const char* s2)
      { return strcasecmp(s1, s2) == 0; }
  };
  
  // freqtable:
  // requirements: write N times, read once
  class freqtable {
    public:
      freqtable() : table() {}
      ~freqtable()
      {
	iterator i;
	for (i = table.begin(); i != table.end(); i++)
	  delete *i;
      }
      
      bool empty() const
      { return table.empty(); }
      
      int size() const
      { return table.size(); }

      void insert(const char* filename, struct stat&, classifier&);
      void pop();  
      entry* top() const
      { return table.front(); }
      
    protected:
      typedef typename std::vector<entry*> table_t;
      typedef typename table_t::iterator iterator;
      
      table_t			table;
      
      long find(const char*, classifier&);

      int left(unsigned int i) 
	{ return 2*i + 1; }
      int right(unsigned int i) 
	{ return 2*i + 2; }
      int parent(unsigned int i) 
	{ return (i-1)/2; }
	
    private:
      void bubbleUp(unsigned int);
      void trickleDown(unsigned int);
  };

  void freqtable::insert(const char* fileid, struct stat& st, classifier& cl)
  {
    long pos = find(fileid, cl);
    
    if ( pos == -1 ) {
      table.push_back(new entry(fileid, st));
      pos = table.size() - 1;
    } else {
      table.at(pos)->add(st);
    }
    
    bubbleUp(pos);
  }

  long freqtable::find(const char* x, classifier& cl)
  {
    unsigned int n = table.size();
    
    for (unsigned int i = 0; i < n; i++)
      if ( cl(table.at(i)->getid(), x) )
	return i;
      
    return -1;
  }

  void freqtable::pop()
  {
    delete table.front();		// delete the actual element
    table.front() = table.back();	// overwrite first with last address
    table.pop_back();
    trickleDown(0);			// trickle last address to new its place
  }


  // CHARTABLE PRIVATE FUNCTIONS
  void freqtable::trickleDown(unsigned int i)
  {
    unsigned int k = table.size();
    
    while (1) {
      unsigned int j;
      unsigned int r = right(i);
      unsigned int l = left(i);

      if ( ( r < k ) && (table[r]->getcount() > table[i]->getcount()) ) {
	// both/right element(s) smaller than t(i)
	
	j = (table[l]->getcount() > table[r]->getcount()) ? l : r;
      } else if ( ( l < k ) && (table[l]->getcount() > table[i]->getcount()) ) {
	// left element smaller than t(i)
	
	j = l;
      } else {
	// element in place
	
	break;
      }
      
      std::swap(table[i], table[j]);
      i = j;
    }
  }
  
  void freqtable::bubbleUp(unsigned int i)
  {
    unsigned int p = parent(i);
    while ( (i > 0) && (table[i]->getcount() > table[p]->getcount()) ) {
      std::swap(table[i], table[p]);
      i = p;
      p = parent(i);
    }
  }

}
  
namespace directory {
  
  class handle {
    public:
      // standard constructor
      handle() : dir(0), f_open(false), f_eof(false) {}

      // convinience constructor
      handle(const char* dirname) : dir(0), f_open(false), f_eof(false)
	{ open(dirname); }
      
      ~handle()
	{ close(); }
      
      // open & close
      void open(const char* dirname)
      { 
	if (! f_open) {
	  dir = opendir(dirname); 
	  f_open = (dir != 0);
	}
      }
      
      void close()
      { if (f_open) {closedir(dir); f_open = false;} }
      
      // read
      dirent* read()
      { 
	dirent* buf;
	if (! good() )
	  return 0;
	
	do {
	  buf = readdir(dir);
	  f_eof = (buf == 0);
	} while ( good() && is_dot_or_dotdot(&(buf->d_name[0])) );
	
	return buf;
      }
      
      // errors
      bool is_open() const
	{ return f_open; }
      bool eof() const
	{ return f_eof; }
      bool good() const
	{ return f_open & (! f_eof); }
    private:
      bool is_dot_or_dotdot(const char* name)
      {
	return (name[0] == '.') && ((name[1] == '\0') || ((name[1] == '.') && (name[2] == '\0')));
      }
      
      DIR* dir;
      
      bool f_open;
      bool f_eof;
  };
  
}

bool parse_dir(const char* dirname, type::freqtable& outdev, type::classifier& cl) 
{
  
  LOG(2, "[NTFY] entering directory \"" << dirname << '"' << std::endl;);
  
  if (chdir(dirname) == -1) {
    LOG(2, "[WARN] failed to chdir to directory: \"" << dirname << '"' << std::endl);
    return false;
  }
  
  directory::handle dirhand(".");
  
  if ( dirhand.is_open() ) {
    
    while (1) {
      dirent* d = dirhand.read();
      if ((! d) || (! dirhand.good()))
	break;

      char* ptr = &(d->d_name[0]);		// pointer to entry name
      struct stat stbuf;
      #ifdef HAVE_LSTAT
      if (globaloptions.dereference) {
	if (stat(ptr, &stbuf) == -1)		// stat entry name
	  continue;
      } else {
	if (lstat(ptr, &stbuf) == -1)		// lstat entry name
	  continue;
      }
      #else
      if (stat(ptr, &stbuf) == -1)
	continue;
      #endif // HAVE_LSTAT
      
      switch(stbuf.st_mode & S_IFMT) {
	// FILE
	case S_IFREG:
	  ptr = strrchr(ptr, '.');		// get id
	  if (! ptr)				// no id -> ignore
	    break;
	  if (*ptr++ == '\0')			// null id -> ignore
	    break;

	  LOG(3, "[DBG] adding \"" << &(d->d_name[0]) << "\" as \"" << ptr << '"' << std::endl);
	  
	  outdev.insert(ptr, stbuf, cl);	// feed id to frequency table
	  break;
	  
	// DIRECTORY
	case S_IFDIR:
	  
	  // logging printed by child
	  if (globaloptions.recursive)
	    parse_dir(ptr, outdev, cl);

	  break;
	
	// OTHER
	default:
	  LOG(3, "[DBG] ignoring \"" << ptr << "\" (not a file)" << std::endl);
	  break;
      }
    }
    
    dirhand.close();
    
  } else { // dirhand.is_open()
    LOG(1, "[WARN] failed to open directory: \"" << dirname << '"' << std::endl);
    return false;
  } // dirhand.is_open()

  if (chdir("..") == -1)
    return false;

  LOG(2, "[NTFY] returning from directory \"" << dirname << '"' << std::endl);
  
  return true;
}

void printprettysize(off_t bytes, std::ostream& out)
{
  const char units[] = " kMGTPEZY";  
  int i;
  
  for (i = 0; bytes > 1024; i++)
      bytes /= 1024;
  
  out.width(4);
  out << bytes;
  out << ' ' << units[i] << 'b';
}  
  

void printhelp(const char* name)
{
  std::cout << "Usage: " << name << " [OPTION] [directory ...]" << std::endl;
  std::cout << "  -r, --recursive\tRecursively expand directories" << std ::endl;
  std::cout << "  -i, --insensitive\tClassify case-insensitively" << std ::endl;
  std::cout << "  -h, --human-readable\tPrint human-readable sizes" << std ::endl;
  #ifdef HAVE_LSTAT
  std::cout << "  -L, --dereference\tDereference links" << std ::endl;
  #endif
  std::cout << "  -q, --quiet\t\tonly errors to STDERR" << std::endl;
  std::cout << "  -v, --verbose\t\tprint notifies to STDERR" << std::endl;
  std::cout << "  -d, --debug\t\tprint everything to STDERR" << std::endl;
  std::cout << "  --count-links\t\tcount links instead of inodes" << std::endl;
  
  std::cout << "  -V, --version\t\tPrint version and exit" << std::endl;
  std::cout << "  --help\t\tThis help" << std::endl;
  
  exit(EXIT_SUCCESS);
}

void printversion(const char* name)
{
  std::cout << name << " 0.12" << std::endl;
  exit(EXIT_SUCCESS);
}

int main(int argc, char** argv)
{
  static option lOptions[] = {
    {"recursive", no_argument, 0, 'r'},
    {"insensitive", no_argument, 0, 'i'},
    {"human-readable", no_argument, 0, 'h'},
    {"quiet", no_argument, 0, 'q'},
    {"verbose", no_argument, 0, 'v'},
    {"debug", no_argument, 0, 'd'},
    {"count-links", no_argument, 0, 2},
#ifdef HAVE_LSTAT
    {"dereference", no_argument, 0, 'L'},
#endif
    {"version", no_argument, 0, 'V'},
    {"help", no_argument, 0, 1},
    {0,0,0,0}
  };
  #ifdef HAVE_LSTAT
  static const char* sOptions = "rihqvdLV";
  #else
  static const char* sOptions = "rihqvdV";
  globaloptions.dereference = false;
  #endif
  
  globaloptions.recursive = false;
  globaloptions.case_insensitive = false;
  globaloptions.human_readable = false;
  globaloptions.count_links = false;
  globaloptions.verbosity = 1;
  
  while (1) {
    char c = getopt_long(argc, argv, sOptions, lOptions, 0);
    if (c == -1)
      break;
    
    switch(c) {
      case 'r':
	globaloptions.recursive = true;
	break;
      case 'i':
	globaloptions.case_insensitive = true;
	break;
      case 'h':
	globaloptions.human_readable = true;
	break;
      case 'q':
	globaloptions.verbosity = 0;
	break;
      case 'v':
	globaloptions.verbosity = 2;
	break;
      case 'd':
	globaloptions.verbosity = 3;
	break;
      case 'V':
	printversion(argv[0]);
	// program halted by subroutine
      case 1:
	printhelp(argv[0]);
	// program halted by subroutine
      case 2:
	globaloptions.count_links = true;
	break;
    }
  }
  
  type::classifier* cl;  
  cl = (globaloptions.case_insensitive) ? 
	static_cast<type::classifier*> (new type::case_insensitive_classifier) :
	static_cast<type::classifier*> (new type::binary_classifier);
  
  // read the directories specified
  type::freqtable storage;
  if (optind < argc) {
    for (int i = optind; i < argc; i++) 
      if (! parse_dir( argv[i], storage, *cl) )
	LOG(1, "[WARN] failed to read directory: \"" << argv[i] << '"' << std::endl);
  } else {
    if (! parse_dir( ".", storage, *cl) )      
      LOG(1, std::cerr << "[WARN] failed to read directory: \".\" (self)" << std::endl);
  }
  
  // print errors
  if ( storage.empty() ) {
    LOG(0, "[ERROR] no files read in specified directories" << std::endl);
    exit(EXIT_FAILURE);
  }
  
  std::cout.width(10);
  std::cout << "file id";
  
  std::cout << " devs ";
  
  std::cout << ((globaloptions.count_links) ? "links  " :"inodes ");
  
  if (! globaloptions.human_readable)
    std::cout.width(22);
  
  std::cout << "size";
  std::cout << std::endl;
  
  // print results
  type::entry* tmpent;
  while (! storage.empty() ) {
    tmpent = storage.top();
    
    if (! globaloptions.count_links)
      tmpent->remove_duplicates();
    
    std::cout.width(10);
    std::cout << tmpent->getid();
    std::cout << ' ';
    
    std::cout.width(4);
    std::cout << tmpent->getdevices();
    std::cout << ' ';
    
    std::cout.width(6);
    std::cout << tmpent->getcount();
    std::cout << ' ';
    
    if (globaloptions.human_readable) {
      printprettysize(tmpent->getbytes(), std::cout);
    } else {
      std::cout.width(22);
      std::cout << tmpent->getbytes();
    }
    std::cout << std::endl;

    storage.pop();
  }
  
  exit(EXIT_SUCCESS);
}

#ifndef HAVE_STRCASECMP
static unsigned char casein_charmap[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xe1, 0xe2, 0xe3, 0xe4, 0xc5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

int strcasecmp(const char* s1, const char* s2)
{
  unsigned char u1, u2;

  for ( ; ; s1++, s2++) {
      u1 = (unsigned char) *s1;
      u2 = (unsigned char) *s2;
      if ((u1 == '\0') || (casein_charmap[u1] != casein_charmap[u2])) {
	  break;
      }
  }
  return casein_charmap[u1] - casein_charmap[u2];
}
#endif // HAVE_STRCASECMP

#ifndef HAVE_STRDUP
char* strdup(const char* s1)
{
  int bufsiz = strlen(s1) + 1;  
  char* ret = malloc(bufsiz);
  if (! ret) return ret;
  memcpy(ret, s1, bufsiz);
  return ret;
}
#endif

#ifndef HAVE_STRRCHR
char* strrchr(const char* s1, int c1)
{
  char *ret = s1 + strlen(s1);
  for (; (*ret != c1) && (ret != s1); ret--)
    /* empty */;
  return ret;
}
#endif

