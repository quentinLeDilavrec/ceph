#ifndef __MDISCOVERREPLY_H
#define __MDISCOVERREPLY_H

#include "msg/Message.h"
#include "mds/CDir.h"
#include "mds/CInode.h"
#include "include/filepath.h"

#include <vector>
#include <string>
using namespace std;

// normal: d+inode + (dir + d+inode)*
// root:   inode + dir
// dir dis: dir

#define max(a,b)  ((a)>(b) ? (a):(b))

class MDiscoverReply : public Message {
  inodeno_t    base_ino;
  bool         no_base_dir;
  bool         no_base_dentry;
  //  bool        flag_forward;
  bool        flag_error;
  string      error_dentry;   // dentry that was not found (to trigger waiters on asker)

  // in general, 
  //       dir [ + dentry [ + inode [ + ...repeat... ]]] 
  // where that (dir, dentry, inode) share the same index.
  // we can start and end with any type.
  
  // dir [ + ... ]                 : discover want_base_dir=true
  
  // dentry [ + inode [ + ... ] ]  : discover want_base_dir=false
  //                                 no_base_dir=true
  //  -> we only exclude inode if dentry is null+xlock

  // inode [ + ... ], base_ino = 0 : discover base_ino=0, start w/ root ino,
  //                                 no_base_dir=no_base_dentry=true
  
  vector<CDirDiscover*>   dirs;      // not inode-aligned if no_base_dir = true.
  filepath                path;      // not inode-aligned if no_base_dentry = true
  vector<bool>            path_xlock;  
  vector<CInodeDiscover*> inodes;

 public:
  // accessors
  inodeno_t get_base_ino() { return base_ino; }
  int       get_num_inodes() { return inodes.size(); }
  int       get_num_dentries() { return path.depth(); }
  int       get_num_dirs() { return dirs.size(); }

  int       get_depth() {   // return depth of deepest object (in dir/dentry/inode units)
	return max( inodes.size(),
				max( path.depth() + no_base_dentry + is_flag_error(),
					 dirs.size() + no_base_dir ));
  }

  bool      has_base_dir() { return !no_base_dir; }
  bool      has_base_dentry() { return !no_base_dentry; }
  bool has_root() {
    if (base_ino == 0) {
      assert(no_base_dir && no_base_dentry);
      return true;
    }
	return false;
  }
  string& get_path() { return path.get_path(); }
  bool get_path_xlock(int i) { return path_xlock[i]; }

  //  bool is_flag_forward() { return flag_forward; }
  bool is_flag_error() { return flag_error; }
  string& get_error_dentry() { return error_dentry; }

  // these index _arguments_ are aligned to each ([[dir, ] dentry, ] inode) set.
  CDirDiscover& get_dir(int n) { return *(dirs[n - no_base_dir]); }
  string& get_dentry(int n) { return path[n - no_base_dentry]; }
  bool get_dentry_xlock(int n) { return path_xlock[n - no_base_dentry]; }
  CInodeDiscover& get_inode(int n) { return *(inodes[n]); }
  inodeno_t get_ino(int n) { return inodes[n]->get_ino(); }

  // cons
  MDiscoverReply() {}
  MDiscoverReply(inodeno_t base_ino) :
	Message(MSG_MDS_DISCOVERREPLY) {
	this->base_ino = base_ino;
	//flag_forward = false;
	flag_error = false;
	no_base_dir = no_base_dentry = true;
  }
  ~MDiscoverReply() {
	for (vector<CDirDiscover*>::iterator it = dirs.begin();
         it != dirs.end();
         it++) 
	  delete *it;
    for (vector<CInodeDiscover*>::iterator it = inodes.begin();
         it != inodes.end();
         it++) 
	  delete *it;
  }
  virtual char *get_type_name() { return "DisR"; }
  
  // builders
  bool is_empty() {
    return dirs.empty() && path.depth() == 0 && 
	  inodes.empty() && 
	  //!flag_forward &&
	  !flag_error;
  }
  void set_path(filepath& dp) { path = dp; }
  void add_dentry(string& dn, bool xlock) { 
	if (inodes.empty() && path.depth() == 0) no_base_dentry = false;
    path.add_dentry(dn);
	path_xlock.push_back(xlock);
  }

  void add_inode(CInodeDiscover* din) {
    inodes.push_back( din );
  }

  void add_dir(CDirDiscover* dir) {
	if (inodes.empty()) no_base_dir = false;
    dirs.push_back( dir );
  }

  //  void set_flag_forward() { flag_forward = true; }
  void set_flag_error(string& dn) { flag_error = true; error_dentry = dn; }


  // ...
  virtual void decode_payload(crope& r) {
	int off = 0;
	r.copy(off, sizeof(base_ino), (char*)&base_ino);
    off += sizeof(base_ino);
    r.copy(off, sizeof(bool), (char*)&no_base_dir);
    off += sizeof(bool);
    r.copy(off, sizeof(bool), (char*)&no_base_dentry);
    off += sizeof(bool);
	//    r.copy(off, sizeof(bool), (char*)&flag_forward);
    //off += sizeof(bool);
    r.copy(off, sizeof(bool), (char*)&flag_error);
    off += sizeof(bool);
	error_dentry = r.c_str() + off;
	off += error_dentry.length() + 1;
    
    // dirs
    int n;
    r.copy(off, sizeof(int), (char*)&n);
    off += sizeof(int);
    for (int i=0; i<n; i++) {
      dirs.push_back( new CDirDiscover() );
      off = dirs[i]->_unrope(r, off);
    }
	dout(12) << n << " dirs out" << endl;

    // inodes
    r.copy(off, sizeof(int), (char*)&n);
    off += sizeof(int);
    for (int i=0; i<n; i++) {
      inodes.push_back( new CInodeDiscover() );
      off = inodes[i]->_unrope(r, off);
    }
	dout(12) << n << " inodes out" << endl;

    // filepath
    off = path._unrope(r, off);
	dout(12) << path.depth() << " dentries out" << endl;

	// path_xlock
	r.copy(off, sizeof(int), (char*)&n);
    off += sizeof(int);
    for (int i=0; i<n; i++) {
	  bool b;
	  r.copy(off, sizeof(bool), (char*)&b);
	  off += sizeof(bool);
	  path_xlock.push_back(b);
    }
	
  }
  virtual void encode_payload(crope& r) {
	r.append((char*)&base_ino, sizeof(base_ino));
	r.append((char*)&no_base_dir, sizeof(bool));
	r.append((char*)&no_base_dentry, sizeof(bool));
	//	r.append((char*)&flag_forward, sizeof(bool));
	r.append((char*)&flag_error, sizeof(bool));

	r.append((char*)error_dentry.c_str());
	r.append((char)0);

	// dirs
    int n = dirs.size();
    r.append((char*)&n, sizeof(int));
    for (vector<CDirDiscover*>::iterator it = dirs.begin();
         it != dirs.end();
         it++) 
      r.append((*it)->_rope());
	dout(12) << n << " dirs in" << endl;
    
	// inodes
    n = inodes.size();
    r.append((char*)&n, sizeof(int));
    for (vector<CInodeDiscover*>::iterator it = inodes.begin();
         it != inodes.end();
         it++) 
      r.append((*it)->_rope());
	dout(12) << n << " inodes in" << endl;

	// path
    r.append(path._rope());
	dout(12) << path.depth() << " dentries in" << endl;

	// path_xlock
	n = path_xlock.size();
	r.append((char*)&n, sizeof(int));
	for (vector<bool>::iterator it = path_xlock.begin();
		 it != path_xlock.end();
		 it++) {
	  bool b = *it;
	  r.append((char*)&b, sizeof(bool));
	}
  }

};

#endif
