m3dsync
=======

This utililty helps you to synchronize large diverged media file collections (like movies and music).
Its design scenario is that each collection is stored on a different computer,
but it also works to synchronize directories on the same computer.
It creates a fingerprint for each file and stores them all in a database file.
It reads not more than 50MB from the end of each file, because this turned out to be unique enough.
It has special support for mp3 files: they will be identified even if the ID3 tags have changed.

To compile,
- get a compiler that supports C++11 / C++0x (like a recent version of gcc or clang),
- install cmake and the crypto++ library including headers, and
- run: `cmake . && make`

If Alice an Bob want to sync _/mnt/A_ and _/mnt/B_, here is what they should do:
- Alice runs: `m3dsync scan A.dat /mnt/A`
- Alice sends _A.dat_ to Bob
- Bob runs: `m3dsync scan B.dat /mnt/B`
- Bob runs: `m3dsync comp A.dat B.dat`
- The last command created _copy-from-A.sh_ and _copy-from-B.sh_.
- Bob mounts an external drive on _/mnt/drive_.
- Bob runs: `./copy-from-B.sh /mnt/drive`
- Bob unmounts, sends the drive and the file _copy-from-A.sh_ to Alice.
- Alice mount the drive, which has all the files she misses. She moves the file from the drive.
- Alice runs: `./copy-from-A.sh /mnt/drive`
- Alice sends the drive to Bob, who now gets all the files he misses.

Done.
