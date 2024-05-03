# DFS
A Distributed File System (DFS) is a client/server-based application that allows a client to store and retrieve files on multiple servers.  One of the features of a DFS is that each file is divided into chunks and stored on different servers and can be reconstructed even if one of the servers is not responding.

In this assignment, a DFS client (DFC) uploads and downloads to and from some number of (4 for the following discussion) distributed file servers (DFS1, DFS2, DFS3 and DFS4).  For our purposes, the DFS servers can all be running locally on a single machine with different port numbers, for example from 10001 to 10004.

When the DFC wants to upload a file to the DFS servers, it first splits the file into 4 equal length chunks P1, P2, P3, P4 (a small length difference is acceptable if the total length is not evenly divisable by 4).  The DFC then groups the 4 chunks into 4 pairs such as (P1, P2), (P2, P3), (P3, P4), (P4, P1).  Finally, the DFC uploads the pairs to the 4 DFS servers.  The stored file now has (limited) redundancy - one failed server will not affect the integrity of the file.

 

How to choose which file chunks go where
In order to balance where chunks end up, you will need to hash the filename and then apply a modulus:

Let x = HASH(filename) % y, where Y is the number of DFS servers available.

The table below shows how to determine where chunks reside based on the value x, assuming y = 4:

<img width="230" alt="image" src="https://github.com/gehna-anand/DFS/assets/147139177/e8b27a2d-8fba-4e83-9b83-657c7930bd31">

You can use the md5sum command or your choice of an md5 hash library to compute MD5HASH().


Requirements for the DFC
The client needs to be invoked with the following syntax:

![image](https://github.com/gehna-anand/DFS/assets/147139177/33ff70a1-5dc3-4ab5-9e6e-5420ab2edcbf)

The configuration file ~/dfc.conf should contain the list of DFS server addresses and port numbers:

![image](https://github.com/gehna-anand/DFS/assets/147139177/16f8a9e3-2877-4227-bc05-e0efa46bebd1)

## The DFC should provide for 3 commands list, get and put:

The list command inquires what files are stored on DFS servers, and should print the filenames available.  The list command should also be able to identify if the file chunks on the available DFS servers are enough to reconstruct the original file.  If pieces are not enough (means some servers are not available) then “[incomplete]” will be added to the end of the file.

The get command downloads all available chunks of a file from all available DFS servers.  If the file can be reconstructed then write it to the current working directory.  If not, then print the error <filename> is incomplete, where <filename> is replaced by the actual filename.

The put command uploads the indicated file(s) into the DFS.  If there are not enough servers to store the file reliably, your program should respond with <filename> put failed.


Requirements for the DFS:
The DFS servers should be invoked by the follow commands:

<img width="200" alt="image" src="https://github.com/gehna-anand/DFS/assets/147139177/26c70f54-f180-4ed3-841a-dc46cd6238b3">

Each DFS server should have its own directory named dfs1, dfs2, dfs3, or dfs4, under the server's current working directory.

A client must try for 1 second to connect to the server. If a DFS server does not respond in 1 second, we consider that server is not available.

Your DFS servers must handle multiple connections and service multiple DFCs concurrently.
