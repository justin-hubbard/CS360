// 360 Lab 6
// Justin Hubbard
// This file is for my own personal use, no one else's

#include "types.h"
//#include "util.c"
//#include "ls_cd_pwd.c"

#define true 1
#define false 0

int mount_root();
int init();
unsigned long ialloc(int dev);
unsigned long balloc(int dev);
MINODE *iget(int dev, int ino);


MINODE minode[NMINODES];	// array of my inodes
MINODE *root;			// Pointer to / inode
PROC proc[NPROC], *running;		// Array of running processes

char names[64][128], *name[64];	// Array of input names, pointer to current name
int fd, dev, n;
int nblocks, ninodes, bmap, imap, inode_start;
char line[256], cmd[32], pathname[256], thd[32];
//struct stat sStat;

char *disk = "diskimage";
int main (int argc, char argv[])
{
	int ino;
	char buf[BLKSIZE];
	struct stat sStat;

	if (argc > 1)
		disk = argv[1];

	printf("checking EXT2 FS ....");
	if ((fd = open(disk, O_RDWR)) < 0)
	{
		printf("open %s failed\n", disk);
		exit(1);
	}

	dev = fd;

	// Read super block
	get_block(dev, 1, buf);
	sp = (SUPER *)buf;

	// Verify ext2 filesystem
	if (sp->s_magic != 0xEF53)
	{
		printf("magic = %x is not an ext2 filesystem\n", sp->s_magic);
		exit(1);
	}
	printf("FS: OK\n");
	ninodes = sp->s_inodes_count;
	nblocks = sp->s_blocks_count;

	printf("ninodes: %d, nblocks: %d\n", ninodes, nblocks);

	// Load Group Descriptor into memory
	get_block(dev, 2, buf);
	gp = (GD *)buf;

	bmap = gp->bg_block_bitmap;
	imap = gp->bg_inode_bitmap;
	inode_start = gp->bg_inode_table;
	printf("bmap = %d, imap = %d, inode_start = %d\n", bmap, imap, inode_start);
	

	init();
	mount_root();

	// PRINT DIRS TEST
	MINODE *tmp = iget(dev, 16);
	print_dir_entries(root);
	printf("Ino: %d\n", search(&(tmp->INODE), "hugefile"));
	getino(dev, "/");

	printf("root refCount = %d\n", root->refCount); // MUST HAVE WORKING ROOT_MOUNT FIRST
	
	printf("set P0 as running process\n");
	printf("COMPILE TEST!\n");
	running = &proc[0];
	running->status = READY;
	running->cwd = iget(dev, 2);
	printf("root refCount = %d\n", root->refCount);


	print_menu();
	
	// ask for command
	while(1)
	{
		printf("\nEnter a command: \n");
		fgets(line, 128, stdin);

		line[strlen(line)-1] = 0;

		if (line[0] == 0)
			continue;
		pathname[0] = 0;
		thd[0] = 0;

		// Get command and pathname into separate buffers (TOKENIZE IN FUTURE)
		sscanf(line, "%s %s %s", cmd, pathname, thd);
		printf("cmd = %s pathname = %s\n", cmd, pathname);

		if (strcmp(cmd,"ls") == 0)
			my_ls(pathname);
		else if (strcmp(cmd, "cd") == 0)
			my_cd(pathname);
		else if (strcmp(cmd, "pwd") == 0)
			my_pwd(running->cwd, 0);
		else if (!strcmp(cmd, "stat"))
			my_stat(pathname, &sStat);
		else if (!strcmp(cmd, "mkdir"))
			my_mkdir(pathname);
		else if (!strcmp(cmd, "rmdir"))
			my_rmdir(pathname);
		else if (!strcmp(cmd, "rm"))
			my_rm(pathname);
		else if (!strcmp(cmd, "creat"))
			my_creat(pathname);
		else if (!strcmp(cmd, "link"))
			my_link(pathname, thd);
		else if (!strcmp(cmd, "symlink"))
			my_symlink(pathname, thd);
		else if (!strcmp(cmd, "touch"))
			my_touch(pathname);
		else if (!strcmp(cmd, "unlink"))
			my_unlink(pathname);
		else if (!strcmp(cmd, "chmod"))
			my_chmod(pathname, thd);
		else if (strcmp(cmd, "quit") == 0)
			quit();
		else
			printf("Invalid command!\n");


		printf("\ngid: %d\n", (int)sStat.st_gid);


	}

	return;
}

int init()
{
	int i, j;
	MINODE *mip;
	PROC *p;

	printf("init()\n");

	// Initialize minode arrays
	for (i = 0; i < NMINODES; i++)
	{
		mip = &minode[i];
		mip->dev = mip->ino = 0;
		mip->refCount = 0;
		mip->mounted = 0;
		mip->mountptr = 0;
	}

	// Initialize process array
	for (i = 0; i < NPROC; i++)
	{
		p = &proc[i];
		p->pid = i;
		p->uid = 0;
		p->cwd = 0;
		p->status = FREE;

		// Zero out process' file descriptor array
		for (j=0; j < NFD; j++)
		{
			p->fd[j] = 0;
		}
	}
}

int print_menu()
{
	printf ("Available commands:\n");
	printf ("[ cd | ls | pwd | stat | mkdir | rmdir | creat | touch | quit ]\n");
	printf ("[ link | symlink | rm | unlink ]\n");
	return 1;
}

// Mount root inode
int mount_root()
{
	printf("mount_root()\n");
	root = iget(dev, 2);
	proc[0].cwd = iget(dev, 2);
	proc[1].cwd = iget(dev, 2);

	return 1;
}

MINODE *iget(int dev, int ino)
{
	int new = 0, blk = 0, bit = 0;
	char buf[BLKSIZE];

	int i;
	// Loop through inodes to see if ino is already stored
	for (i = 0; i < NMINODES; i++)
	{
		// If current inode is already in buffer, increment refCount
		// then return address to inode of ino#
		if (minode[i].ino == ino && minode[i].refCount > 0)
		{
			minode[i].refCount++;
			return &minode[i];
		}
	}

	for (i = 0; i < NMINODES; i++)
	{
		// Set current new inode to current inode index
		if (minode[i].refCount == 0)
		{
			minode[i].refCount++;
			new = i;
			break;
		}
	}

	// Mailman's algorithm to get block#/bit#
	blk = (ino -1) / (BLKSIZE/sizeof(INODE)) + gp->bg_inode_table;
	bit = (ino -1) % (BLKSIZE/sizeof(INODE));
	get_block(dev, blk, buf);

	minode[new].dev = dev;
	minode[new].dirty = 0;
	minode[new].ino = ino;

	// Copy this inode to the inode pointed to by the MINODE
	memcpy(&minode[new].INODE, (INODE *)buf + bit, sizeof(INODE));
	minode[new].refCount = 1;

	return &minode[new];
}


int iput(MINODE *mip)
{
	int blk, bit;
	INODE *temp;
	char buf[BLKSIZE];

	// Decrement refCount first so we can test
	// if it's still in use elsewhere
	mip->refCount--;

	// If inode is in use, or is unchanged,
	// return and write nothing
	if (mip->refCount > 0)
		return;
	if(mip->dirty == 0)
		return;


	blk = (mip->ino -1) / (BLKSIZE/sizeof(INODE)) + gp->bg_inode_table;
	bit = (mip->ino -1) % (BLKSIZE/sizeof(INODE));

	get_block(fd, blk, buf);

	memcpy((INODE *)buf+bit, &mip->INODE, sizeof(INODE));

	put_block(fd, blk, buf);

}

// Iterates through Inodes looking for inode denoted by name
// seeking to blocks successively
int print_dir_entries(MINODE *mip)
{
	int i; 
	char *cp, sbuf[BLKSIZE];
	DIR *dp;
	INODE *ip;

	ip = &(mip->INODE);
	for (i=0; i<12; i++)
	{  // ASSUME DIRs only has 12 direct blocks
	    if (ip->i_block[i] == 0)
	        return 0;

	    get_block(fd, ip->i_block[i], sbuf);
	    dp = (DIR *)sbuf;
	    cp = sbuf;
	    printf("ino rec_len name_len name\n");
	    while (cp < sbuf + BLKSIZE)
	    {
	        printf("%3d  %6d  %7d  %.4s\n",dp->inode, dp->rec_len, dp->name_len, dp->name);

	        cp += dp->rec_len;
	        dp = (DIR *)cp;
	    }
	}
	   return 0;
}

int search(INODE *ip, char *name)
{
	int i; 
	char *cp, sbuf[BLKSIZE], nameBuf[256];
	DIR *dp;
	//INODE *ip;

	//ip = &(mip->INODE);
	for (i=0; i<12; i++)
	{  // ASSUME DIRs only has 12 direct blocks
	    if (ip->i_block[i] == 0)
	        return 0;

	    get_block(fd, ip->i_block[i], sbuf);
	    dp = (DIR *)sbuf;
	    cp = sbuf;
	    while (cp < sbuf + BLKSIZE)
	    {
	    	sprintf(nameBuf, "%s", dp->name);

	    	if (!strcmp(nameBuf, name))
	    		return dp->inode;
	    	else
	    	{
	        	cp += dp->rec_len;
	        	dp = (DIR *)cp;
	        	// Nake sure that new directory isn't null
	        	//if ((ip->i_block[i+1] == 0) && (dp->inode == 0))
	        		//printf("NULL\n");
	        }
	    }
	}
	printf("Search reached end of function\n");
	return 0;
}


int getino(int *dev, char *pathname)
{
	printf("\n\n*********START GETINO**********\n");

	int ino, i;
	char path[256], *token, buf[BLKSIZE];
	INODE tempInode;
	MINODE *tempMI;

	strncpy(path, pathname, 256);

	// Absolute filepath
	if (pathname[0] == '/')
	{
		dev = root->dev;
		printf("getino Path: %s\n", path);
		strncpy(path, path+1, 255); // Remove first slash so strtok works better
		ino = 2; // Set ino to 2 in case name ultimately refers to '/'
	}

	// Start tokenizing path to search by consecutive names
	token = strtok(path, "/");

	tempInode = root->INODE;
	printf("Inode assignment good\n");

	// Now loop searches over consecutive names
	while (token != NULL)
	{
		printf("Cur name: %s\n", token);
		ino = search(&tempInode, token);
		printf("Search ino: %d\n", ino);

		if (ino == 0)
			return ino;
		tempMI = iget(dev, ino);
		tempInode = tempMI->INODE;

		token = strtok(NULL, "/");
	}

	/*}
	// Relative filepath
	else
	{
		printf("RELATIVE\n");
		dev = running->cwd->dev;
	}*/
	return ino;
}

int get_block(int fd, int blk, char buf[ ])
{
  lseek(fd, (long)blk*BLKSIZE, 0);
  read(fd, buf, BLKSIZE);
}

int put_block(int fd, int blk, char buf[ ])
{
  lseek(fd, (long)blk*BLKSIZE, 0);
  write(fd, buf, BLKSIZE);
}


int my_ls(char *pathname)
{
	MINODE *mip;
	INODE *ip;
	int ino;
	int dev;
	char sbuf[BLKSIZE], *cp, name[256];
	DIR *dp;

	dev = running->cwd->dev;

	// No input pathname, just ls the cwd
	if (pathname[0] == 0)
	{
		ino = running->cwd->ino;
		mip = iget(dev, ino);
	}
	else // Use pathname to establish correct 'root' device
	{
		if (pathname[0] == '/')
			dev = root->dev;

		ino = getino(&dev, pathname);

		// If ino is zero, fail
		if (ino == 0)
			return 1;

		mip = iget(dev, ino);


		if (!S_ISDIR(mip->INODE.i_mode))
		{
			printf("bad path, not directory\n");
			return 1;
		}
	}

	ip = &mip->INODE;

	// Loop through blocks to find name
	for (int i = 0; i < 12; i++)
	{
		if (ip->i_block[i])
		{
			get_block(dev, ip->i_block[i], sbuf);
			dp = (DIR *)sbuf;
	    	cp = sbuf;

	    	// loop through the blocks
	    	while (cp < sbuf + BLKSIZE)
	    	{
	    		// Get diretory name for dir to give to printer
	    		strncpy(name, dp->name, dp->name_len);
	    		name[dp->name_len] = '\0';

	    		// Get ino number from directory
	    		ino = dp->inode;
	    		mip = iget(dev, ino);

	    		// Send minode and dir name to be printed
	    		ls_printer(name, mip);

	    		// Increment block/dir pointers
	    		cp += dp->rec_len;
	    		dp = (DIR *)cp;
	    	}
		}
	}

	// print dirs

	iput(mip);
	return 0;


}

int ls_printer(char *dirName, MINODE *prnt)
{
	INODE *ip;
	char c, *time, dir[256];

	ip = &prnt->INODE;

	// Print file mode
	if(S_ISDIR(ip->i_mode))
		printf("d");
	else if (S_ISREG(ip->i_mode))
		printf("-");
	else if(S_ISLNK(ip->i_mode))
		printf("l");
	else
		printf("-");

	// Permissions checking
	for (int i = 8; i >= 0; i--)
	{
		if (i % 3 == 2)
		{
			if (ip->i_mode & (1 << i))
				c = 'r';
			else
				c = '-';
		}
		if (i % 3 == 1)
		{
			if (ip->i_mode & (1 << i))
				c = 'w';
			else
				c = '-';
		}
		if (i % 3 == 0)
		{
			if (ip->i_mode & (1 << i))
				c = 'x';
			else
				c = '-';
		}

		putchar(c);
	}

	// Other info/name printing
	printf(" %d %d %d %.4d", ip->i_links_count, ip->i_uid, ip->i_gid, ip->i_size);
	time = ctime(&(ip->i_mtime));
	time[strlen(time)-1] = '\0';
	printf(" %s ", time);


	// Symlinked files have the pathname instead of blocks
	if (S_ISLNK(ip->i_mode))
	{
		printf("%.20s -> %.20s\n", dirName, (char *)prnt->INODE.i_block);
	}
	else
	{
		printf("%.20s\n", dirName);
	}


}

int my_cd(char *path)
{	

	int ino;
	MINODE *temp;

    // If no path given, set running process' cwd
    // to root /
    if (pathname[0] <= 0)
    {
    	running->cwd = iget(root->dev, 2);
    	return;
    }
    // If path specifies the root, do the same thing
    if (strcmp(pathname, "/") == 0)
    {
    	running->cwd = iget(root->dev, 2);
    	return;
    }

    ino = getino(dev, pathname);
    printf("CD INO: %d\n", ino);

    if (ino == 0) // Directory no found
    	return 0;

    temp = iget(root->dev, ino);
    printf("IGOT\n");

    printf("I Mode: %d\n", temp->INODE.i_mode);
    if ((temp->INODE.i_mode & 0100000) == 0100000)
    {
    	iput(temp);
    	printf("ITS NOT A FRIGGIN DIR\n");
    }

    running->cwd = temp;

    return;
}

int my_pwd(MINODE *mip, int sIno)
{
	if (mip->ino == root->ino)
		printf("/");

	char buf[BLKSIZE], pathname[256];
	char *cp;
	DIR *dp;
	MINODE *rmip;
	int ino;

	get_block(dev, mip->INODE.i_block[0], (char *)&buf);
	dp = (DIR *)buf;
	cp = buf + dp->rec_len;
	dp = (DIR *)cp;

	if (mip->ino != root->ino)
	{
		ino = dp->inode;
		rmip = iget(dev, ino);
		my_pwd(rmip, mip->ino);
	}
	if (sIno != 0)
	{
		while(dp->inode != sIno)
		{
			cp += dp->rec_len;
			dp = (DIR *)cp;
		}
		strncpy(pathname, dp->name, dp->name_len);
		pathname[dp->name_len] = '\0';
		printf("%s/", pathname);
	}
	return;
}

int my_stat(char *pathname, struct stat *statPtr)
{
	MINODE *mip;
	INODE *ip;
	int ino;

	ino = getino(running->cwd->dev, pathname);
	mip = iget(running->cwd->dev, ino);
	printf("Ino: %d\n", ino);
	ip = &mip->INODE;

	statPtr->st_dev = running->cwd->dev;
	statPtr->st_blksize = BLKSIZE;

	// Copy stats from INODE to stat pointer
	memcpy(&statPtr->st_ino, &ino, sizeof(ino_t));
	memcpy(&statPtr->st_mode, &ip->i_mode, sizeof(mode_t));
	memcpy(&statPtr->st_nlink, &ip->i_links_count, sizeof(nlink_t));
	memcpy(&statPtr->st_size, &ip->i_size, sizeof(size_t));
	memcpy(&statPtr->st_uid, &ip->i_uid, sizeof(uid_t));
	memcpy(&statPtr->st_gid, &ip->i_gid, sizeof(gid_t));
	memcpy(&statPtr->st_blocks, &ip->i_blocks, sizeof(blkcnt_t));
	memcpy(&statPtr->st_atime, &ip->i_atime, sizeof(time_t));
	memcpy(&statPtr->st_mtime, &ip->i_mtime, sizeof(time_t));
	memcpy(&statPtr->st_ctime, &ip->i_ctime, sizeof(time_t));
	

	// Convert time variables to date formats
	char *ctimeP, *atimeP, *mtimeP;
	ctimeP = ctime(&ip->i_ctime);
	atimeP = ctime(&ip->i_atime);
	mtimeP = ctime(&ip->i_mtime);

	// Print stats
	printf("\n\ngid stat: %d\n", (int)statPtr->st_gid);
	printf("device: %d\n", (int)statPtr->st_dev);
	printf("inode number: %d\n", (int)statPtr->st_ino);
	printf("mode: %d\n", (int)statPtr->st_mode);
	printf("hardlinks: %lu\n", (long)ip->i_links_count);
	printf("uid: %d\n", (int)statPtr->st_uid);
	printf("size: %d\n", (int)statPtr->st_size);
	printf("block size: %d\n", (int)statPtr->st_blksize);
	printf("ctime: %s", ctimeP);
	printf("atime: %s", atimeP);
	printf("mtime: %s\n\n\n", mtimeP);

	iput(mip);

	return;
	
}





//##############################################################
//##############################################################
//##############################################################
//##############################################################
//##############################################################





// MKDIR AND RMDIR

int my_mkdir(char *pathname)
{
	MINODE *cmip, *pmip; // child and parent MINODEs, respectively
	char parentPath[256], childPath[256], pathCpy1[256], pathCpy2[256], newDIR[BLKSIZE], buf[BLKSIZE];
	int ino, Mdev, newLen, realLen, availLen, nwIno, nwBlk, blk = 0;
	DIR *dp;
	char *cp;


	// make sure user provided a pathn to create
	if (pathname[0] == 0)
	{
		printf("No path!\n");
		return 1;
	}

	// Check if path starts with '/'
	if (pathname[0] == '/')
		Mdev = root->dev;
	else
		Mdev = running->cwd->dev;



	// Copy pathname so it can be altered
	strcpy(pathCpy1, pathname);
	strcpy(pathCpy2, pathname);

	// Get parent and child pathnames
	strcpy(parentPath, dirname(pathCpy1));
	strcpy(childPath, basename(pathCpy2));

	printf("Parent: %s, Child: %s\n", parentPath, childPath);

	// Get inode of parent path (where dir will be made)
	ino = getino(Mdev, parentPath);
	pmip = iget(Mdev, ino);

	// Search to make sure dir to be created doesn't already exist
	// Shouldn't be number other than 0 aka shouldn't return real inode #
	if (search(&pmip->INODE, childPath) != 0)
	{
		printf("It already exists dude, cool it\n");
		return 1;
	}

	Mdev = pmip->dev;

	// Allocate new inode and new block
	nwIno = ialloc(Mdev);
	nwBlk = balloc(Mdev);

	printf("new ino: %d, new block: %d\n", nwIno, nwBlk);

	// Get MINODE that was just allocated for the child
	cmip = iget(Mdev, nwIno);

	// Set dir block to start at newly allocated block
	cmip->INODE.i_block[0] = nwBlk;

	// Zero out all 15 blocks
	for (int i = 1; i < 16; i++)
	{
		cmip->INODE.i_block[i] = 0;
	}

	// Set all relevant MINODE members
	cmip->dirty = 1;
	cmip->ino = nwIno;
	cmip->INODE.i_mode = 0x41ED;
	cmip->INODE.i_size = BLKSIZE;
	cmip->INODE.i_links_count = 2;
	cmip->INODE.i_blocks = 2;
	cmip->INODE.i_uid = running->uid;
	cmip->INODE.i_gid = running->gid;
	cmip->INODE.i_atime = cmip->INODE.i_ctime = cmip->INODE.i_mtime = time(0L);

	printf("mkdir child data set\n");

	// Write it back
	iput(cmip);

	// NOW HAVE TO MAKE NEW DIR ENTRY, put . and .. as first two blocks in new dir
	//get_block(dev, nwBlk, buf);

	memset(buf, 0, BLKSIZE);

	dp = (DIR *)buf;
	dp->inode = nwIno;
	dp->name_len = 1; // . is length one
	dp->rec_len = 12;
	strncpy(dp->name, ".", 1);
	
	// Increment to ..
	cp = buf;
	cp += dp->rec_len;
	dp = (DIR *)cp;

	// Set second block to point to parent inode
	dp->inode = pmip->ino;
	dp->name_len = 2; // .. is length two
	dp->rec_len = BLKSIZE - 12; // length is block minus size of /.
	strncpy(dp->name, "..", 2);

	// Put this block officially into fs
	put_block(Mdev, nwBlk, buf);

	printf(". and .. put in\n");

	// GOOD TIL HERE AS FAR AS I KNOW

	//// Now fix the size of the records in parent directory (last is always = remaining space)
	get_block(Mdev, pmip->INODE.i_block[0], buf);

	cp = buf;
    dp = (DIR *)buf;
	
	// Loop to find last entry in block
    while(cp + dp->rec_len < buf + BLKSIZE) {
        cp += dp->rec_len;
        dp = (DIR *)cp;
    }

    printf("end of dir entries found\n");

    realLen = 4*((8+dp->name_len+3)/4); // actual space needed for last entry
	availLen = dp->rec_len - realLen; // get amount of space after real size of last entry

	if (availLen >= realLen) // Enough space for new dir in current block
	{
		// Set previously last dir to it's proper record length
		dp->rec_len = realLen;

		// Move to new dir we just made
		cp += dp->rec_len;
		dp = (DIR *)cp;

		dp->rec_len = availLen; // Set to be size of rest of dir space, whatever it may be
		dp->inode = cmip->ino;
		dp->name_len = strlen(childPath);
		strncpy(dp->name, childPath, strlen(childPath));

		put_block(Mdev, pmip->INODE.i_block[0], buf);
	}
	else
	{	
		// Loop through blocks until one is found where dir will "fit"
		blk = 0;
		while (availLen < realLen)
		{
			blk++;

			if (pmip->INODE.i_block[blk] == 0) // Need to allocate new block
			{
				
				pmip->INODE.i_block[blk] = balloc(Mdev);
				pmip->refCount = 0; // Needed?
				availLen = BLKSIZE;
				memset(buf, 0, BLKSIZE);
				//get_block(dev, pmip->INODE.i_block[blk], buf);
				cp = buf;
				dp = (DIR *)buf;

			}
			else // Try again in block other than the first one SAME AS ABOVE
			{

				get_block(Mdev, pmip->INODE.i_block[blk], buf);
                cp = buf;
                dp = (DIR *) buf;

                // Loop til last entry
                while(cp + dp->rec_len < buf + BLKSIZE)
                {
                    cp += dp->rec_len;
                    dp = (DIR *)cp;
                }

                realLen = 4 * ((8 + dp->name_len + 3) / 4);
				availLen = dp->rec_len - realLen;

                if (availLen >= realLen)
                {
                    dp->rec_len = realLen;
                    cp += dp->rec_len;
                    dp = (DIR *)cp;
                }
			}
		}

		dp->rec_len = availLen; // Set to be size of rest of dir space, whatever it may be
		dp->inode = cmip->ino;
		dp->name_len = strlen(childPath);
		strncpy(dp->name, childPath, strlen(childPath));

		put_block(Mdev, pmip->INODE.i_block[blk], buf);

	}

	// Put it back
	pmip->dirty = 1;
	//pmip->INODE.i_links_count++;
	pmip->refCount++;
	pmip->INODE.i_atime = time(0L); // make last access more accurate
	iput(pmip);
	return;

}


int my_rmdir(char *pathname)
{
	char *cp, namebuf[256], parentPath[256], childPath[256], pathCpy1[256], pathCpy2[256], buf[BLKSIZE];
	MINODE *parent, *toRemove;
	int pino, rino, i;


	if (pathname[0] == 0)
	{
		printf("need a path, dude\n");
		return;
	}

	strcpy(pathCpy1, pathname);
	strcpy(pathCpy2, pathname);

	// Get parent and child pathnames
	strcpy(parentPath, dirname(pathCpy1));
	strcpy(childPath, basename(pathCpy2));

	printf("Parent: %s, Child: %s\n", parentPath, childPath);

	pino = getino(fd, parentPath);
	rino = getino(fd, childPath);

	// Make sure both are valid
	if (pino == 0 || rino == 0)
	{
		printf("your path was not valid\n");
		return;
	}

	parent = iget(fd, pino); // Containing dir MINODE
	toRemove = iget(fd, rino);

	if (!S_ISDIR(toRemove->INODE.i_mode))
	{
		printf("it's gotta be a dir, dude\n");
	}


	// Now make sure dir to remove is in fact empty
	/*
		get_block(fd, toRemove->INODE.i_block[0], buf);

		cp = buf;
		dp = (DIR *)cp; // . entry

		cp += dp->rec_len;
		dp = (DIR *)cp;	// .. entry, rec_len should == 1024 -12 = 1012

		if(dp->rec_len != BLKSIZE - 12)
		{
			printf("dir not empty!\n");
			return;
		}
	*/

	// CHECK IF DIR IS EMPTY
	if(toRemove->INODE.i_links_count == 2) // dirs usualy have two links
	{
		for(i = 0; i <= 11; i++) // loops through direct blocks
		{
			if(toRemove->INODE.i_block[i])
			{
				get_block(toRemove->dev, toRemove->INODE.i_block[i], buf); 
				cp = buf;
				dp = (DIR *)buf;

				while(cp < &buf[BLKSIZE])
				{
					strncpy(namebuf, dp->name, dp->name_len);
					namebuf[dp->name_len] = 0;

					if(strcmp(namebuf, ".") && strcmp(namebuf, ".."))
					{
						printf("Can't remove dir with crap in it!\n");
						return;
					}
					cp+=dp->rec_len;
					dp=(DIR *)cp;
				}
			}
		}
		//return;
	}



	reduce(fd, toRemove);
	iput(toRemove);
	idealloc(fd, toRemove->ino);
	deleteChild(parent, childPath);

	return 0;

}

int my_rm(char *pathname)
{
	char *cp, *parentPath[256], *childPath[256], pathCpy1[256], pathCpy2[256], buf[BLKSIZE];
	MINODE *parent, *toRemove;
	int pino, rino;


	if (pathname[0] == 0)
	{
		printf("need a path, dude\n");
		return;
	}

	strcpy(pathCpy1, pathname);
	strcpy(pathCpy2, pathname);

	// Get parent and child pathnames
	strcpy(parentPath, dirname(pathCpy1));
	strcpy(childPath, basename(pathCpy2));

	printf("Parent: %s, Child: %s\n", parentPath, childPath);

	pino = getino(fd, parentPath);
	rino = getino(fd, childPath);

	// Make sure both are valid
	if (pino == 0 || rino == 0)
	{
		printf("your path was not valid\n");
		return;
	}

	parent = iget(fd, pino); // Containing dir MINODE
	toRemove = iget(fd, rino);

	if (!S_ISREG(toRemove->INODE.i_mode))
	{
		printf("it's gotta be a regular file, dude\n");
	}

	// DO CHECKING

	reduce(fd, toRemove);
	iput(toRemove);
	idealloc(fd, toRemove->ino);
	deleteChild(parent, childPath);

	return 0;
}


int my_creat(char *pathname)
{
	MINODE *cmip, *pmip; // child and parent MINODEs, respectively
	char *parentPath[256], *childPath[256], pathCpy1[256], pathCpy2[256], newDIR[BLKSIZE], buf[BLKSIZE];
	int ino, dev, newLen, realLen, availLen, nwIno, nwBlk, blk = 0;
	DIR *dp;
	char *cp;


	// make sure user provided a pathn to create
	if (pathname[0] == 0)
	{
		printf("No path!\n");
		return 1;
	}

	// Check if path starts with '/'
	if (pathname[0] == '/')
		dev = root->dev;
	else
		dev = running->cwd->dev;



	// Copy pathname so it can be altered
	strcpy(pathCpy1, pathname);
	strcpy(pathCpy2, pathname);

	// Get parent and child pathnames
	strcpy(parentPath, dirname(pathCpy1));
	strcpy(childPath, basename(pathCpy2));

	printf("Parent: %s, Child: %s\n", parentPath, childPath);

	// Get inode of parent path (where dir will be made)
	ino = getino(dev, parentPath);
	pmip = iget(dev, ino);

	// Search to make sure dir to be created doesn't already exist
	// Shouldn't be number other than 0 aka shouldn't return real inode #
	if (search(&pmip->INODE, childPath) != 0)
	{
		printf("It already exists dude, cool it\n");
		return 1;
	}

	dev = pmip->dev;

	// Allocate new inode and new block
	nwIno = ialloc(dev);
	//nwBlk = balloc(dev);

	printf("new ino: %d, new block: %d\n", nwIno, nwBlk);

	// Get MINODE that was just allocated for the child
	cmip = iget(dev, nwIno);

	// Set dir block to start at newly allocated block
	//cmip->INODE.i_block[0] = nwBlk;

	// Zero out all 15 blocks
	for (int i = 0; i < 15; i++)
	{
		cmip->INODE.i_block[i] = 0;
	}

	// Set all relevant MINODE members
	cmip->dirty = 1;
	cmip->ino = nwIno;
	cmip->INODE.i_mode = 0x81A4;
	cmip->INODE.i_size = 0;
	cmip->INODE.i_links_count = 1;
	cmip->INODE.i_blocks = 2;
	cmip->INODE.i_uid = running->uid;
	cmip->INODE.i_gid = running->gid;
	cmip->INODE.i_atime = cmip->INODE.i_ctime = cmip->INODE.i_mtime = time(0L);

	// Write it back
	iput(cmip);

	//get_block(dev, nwBlk, buf);

	memset(buf, 0, BLKSIZE);


	/*
	dp = (DIR *)buf;
	dp->inode = nwIno;
	dp->name_len = 1; // . is length one
	dp->rec_len = 12;
	strncpy(dp->name, ".", 1);
	
	// Increment to ..
	cp = buf;
	cp += dp->rec_len;
	dp = (DIR *)cp;

	// Set second block to point to parent inode
	dp->inode = pmip->ino;
	dp->name_len = 2; // .. is length two
	dp->rec_len = BLKSIZE - 12; // length is block minus size of /.
	strncpy(dp->name, "..", 2);

	// Put this block officially into fs
	put_block(dev, nwBlk, buf);*/

	// GOOD TIL HERE AS FAR AS I KNOW

	//// Now fix the size of the records in parent directory (last is always = remaining space)
	get_block(dev, pmip->INODE.i_block[0], buf);

	cp = buf;
    dp = (DIR *)buf;
	
	// Loop to find last entry in block
    while(cp + dp->rec_len < buf + BLKSIZE) {
        cp += dp->rec_len;
        dp = (DIR *)cp;
    }

    realLen = 4*((8+dp->name_len+3)/4); // actual space needed for last entry
	availLen = dp->rec_len - realLen; // get amount of space after real size of last entry

	if (availLen >= realLen)
	{
		// Set previously last dir to it's proper record length
		dp->rec_len = realLen;

		// Move to new dir we just made
		cp += dp->rec_len;
		dp = (DIR *)cp;

		dp->rec_len = availLen; // Set to be size of rest of dir space, whatever it may be
		dp->inode = cmip->ino;
		dp->name_len = strlen(childPath);
		strncpy(dp->name, childPath, strlen(childPath));

		put_block(dev, pmip->INODE.i_block[0], buf);
	}
	else
	{	
		// Loop through blocks until one is found where dir will "fit"
		blk = 0;
		while (availLen < realLen)
		{
			blk++;

			if (pmip->INODE.i_block[blk] == 0) // Need to allocate new block
			{
				
				pmip->INODE.i_block[blk] = balloc(dev);
				pmip->refCount = 0; // Needed?
				availLen = BLKSIZE;
				memset(buf, 0, BLKSIZE);
				//get_block(dev, pmip->INODE.i_block[blk], buf);
				cp = buf;
				dp = (DIR *)buf;

			}
			else // Try again in block other than the first one SAME AS ABOVE
			{

				get_block(pmip->dev, pmip->INODE.i_block[blk], buf);
                cp = buf;
                dp = (DIR *) buf;

                // Loop til last entry
                while(cp + dp->rec_len < buf + BLKSIZE)
                {
                    cp += dp->rec_len;
                    dp = (DIR *)cp;
                }

                realLen = 4 * ((8 + dp->name_len + 3) / 4);
				availLen = dp->rec_len - realLen;

                if (availLen >= realLen)
                {
                    dp->rec_len = realLen;
                    cp += dp->rec_len;
                    dp = (DIR *)cp;
                }
			}
		}

		dp->rec_len = availLen; // Set to be size of rest of dir space, whatever it may be
		dp->inode = cmip->ino;
		dp->name_len = strlen(childPath);
		strncpy(dp->name, childPath, strlen(childPath));

		put_block(pmip->dev, pmip->INODE.i_block[blk], buf);

	}

	// Put it back
	pmip->dirty = 1;
	//pmip->INODE.i_links_count++;
	pmip->refCount++;
	pmip->INODE.i_atime = time(0L); // make last access more accurate
	iput(pmip);
	return nwIno;//pmip->ino;
}



//############################################################################
//############################################################################
//########################### linking functions ##############################
//############################################################################
//############################################################################


int my_link(char *source, char *link)
{
	MINODE *smip, *lpmip, *lmip; // source, link parent and link child MINODEs, respectively
	char *linkParentPath[256], *linkChildPath[256], pathCpy1[256], pathCpy2[256], newDIR[BLKSIZE], buf[BLKSIZE];
	int ino, Ldev, newLen, realLen, availLen, srcIno, lnkPIno, blk = 0;
	DIR *dp;
	char *cp;




	//if (!(old[0]==0 && new[0]==0))
	if(source[0]==0 || link[0]==0)
	{
		printf("you gotta give me two good paths, dude!\n");
		return 1;
	}

	// Set proper dev
	if(source[0] == '/')
		Ldev = root->dev;
	else
		Ldev = running->cwd->dev;


	// Get ino of the source file
	srcIno = getino(Ldev, source);
	if (srcIno == 0)
		return 1;


	// Get minode to check if it the right type
	smip = iget(Ldev, srcIno);

	// make sure source is a regular file
	if (!S_ISREG(smip->INODE.i_mode))
	{
		iput(smip);
		printf("Has to be a regular file, homie\n");
		return 1;
	}

	//iput(smip); // Put file back, only needed the inode

	// Copy pathname so it can be altered
	strcpy(pathCpy1, link);
	strcpy(pathCpy2, link);

	// Get parent and child pathnames
	strcpy(linkParentPath, dirname(pathCpy1));
	strcpy(linkChildPath, basename(pathCpy2));

	// Get ino # and MINODE for containing parent dir
	lnkPIno = getino(Ldev, linkParentPath);
	lpmip = iget(Ldev, lnkPIno);

	// Check that input path for hardlink doens't exist already
	if (search(&lpmip->INODE, linkChildPath) != 0)
	{
		iput(lpmip);
		printf("the path you gave already exists!\n");
		return 1;
	}

	////9
	get_block(Ldev, lpmip->INODE.i_block[0], buf);

	cp = buf;
    dp = (DIR *)buf;
	
	// Loop to find last entry in block
    while(cp + dp->rec_len < buf + BLKSIZE) {
        cp += dp->rec_len;
        dp = (DIR *)cp;
    }

    realLen = 4*((8+dp->name_len+3)/4); // actual space needed for last entry
	availLen = dp->rec_len - realLen; // get amount of space after real size of last entry

	if (availLen >= realLen)
	{
		// Set previously last dir to it's proper record length
		dp->rec_len = realLen;

		// Move to new dir we just made
		cp += dp->rec_len;
		dp = (DIR *)cp;

		dp->rec_len = availLen; // Set to be size of rest of dir space, whatever it may be
		dp->inode = smip->ino;
		dp->name_len = strlen(linkChildPath);
		strncpy(dp->name, linkChildPath, strlen(linkChildPath));

		put_block(Ldev, lpmip->INODE.i_block[0], buf);
	}
	else
	{	
		// Loop through blocks until one is found where dir will "fit"
		blk = 0;
		while (availLen < realLen)
		{
			blk++;

			if (lpmip->INODE.i_block[blk] == 0) // Need to allocate new block
			{
				
				lpmip->INODE.i_block[blk] = balloc(Ldev);
				lpmip->refCount = 0; // Needed?
				availLen = BLKSIZE;
				memset(buf, 0, BLKSIZE);
				//get_block(dev, pmip->INODE.i_block[blk], buf);
				cp = buf;
				dp = (DIR *)buf;

			}
			else // Try again in block other than the first one SAME AS ABOVE
			{

				get_block(Ldev, lpmip->INODE.i_block[blk], buf);
                cp = buf;
                dp = (DIR *) buf;

                // Loop til last entry
                while(cp + dp->rec_len < buf + BLKSIZE)
                {
                    cp += dp->rec_len;
                    dp = (DIR *)cp;
                }

                realLen = 4 * ((8 + dp->name_len + 3) / 4);
				availLen = dp->rec_len - realLen;

                if (availLen >= realLen)
                {
                    dp->rec_len = realLen;
                    cp += dp->rec_len;
                    dp = (DIR *)cp;
                }
			}
		}

		dp->rec_len = availLen; // Set to be size of rest of dir space, whatever it may be
		dp->inode = smip->ino;
		dp->name_len = strlen(linkChildPath);
		strncpy(dp->name, linkChildPath, strlen(linkChildPath));

		put_block(Ldev, lpmip->INODE.i_block[blk], buf);

	}

	// Put it back
	lpmip->dirty = 1;
	//
	lpmip->refCount++;
	lpmip->INODE.i_atime = time(0L); // make last access more accurate
	iput(lpmip);
	smip->INODE.i_links_count++;
	iput(smip);
	printf("link done\n");
	return;

}

int my_unlink (char *pathname)
{
	char *cp, *parentPath[256], *childPath[256], pathCpy1[256], pathCpy2[256], buf[BLKSIZE];
	MINODE *parent, *toRemove;
	int pino, rino;


	if (pathname[0] == 0)
	{
		printf("need a path, dude\n");
		return;
	}

	strcpy(pathCpy1, pathname);
	strcpy(pathCpy2, pathname);

	// Get parent and child pathnames
	strcpy(parentPath, dirname(pathCpy1));
	strcpy(childPath, basename(pathCpy2));

	printf("Parent: %s, Child: %s\n", parentPath, childPath);

	pino = getino(fd, parentPath);
	rino = getino(fd, childPath);

	// Make sure both are valid
	if (pino == 0 || rino == 0)
	{
		printf("your path was not valid\n");
		return;
	}

	parent = iget(fd, pino); // Containing dir MINODE
	toRemove = iget(fd, rino);

	if (!S_ISREG(toRemove->INODE.i_mode))
	{
		printf("it's gotta be a hardlink, dude\n");
	}


	toRemove->INODE.i_links_count--; // Decrement number of links, obviously

	// Only remove if no longer linking
	if (toRemove->INODE.i_links_count == 0)
	{
		reduce(fd, toRemove);
		toRemove->refCount++;
		toRemove->dirty = 1;
		iput(toRemove);
		idealloc(fd, toRemove->ino);
		deleteChild(parent, childPath);
	}
	else { iput(toRemove); } // No need to do anything, just put it back
	
}


int my_symlink (char *source, char *link)
{
	MINODE *pmip, *symip; // source, link parent and link child MINODEs, respectively
	char *linkParentPath[256], *linkChildPath[256], pathCpy1[256], pathCpy2[256];
	int Ldev, srcIno, lnkPIno, symIno;

	//if (!(old[0]==0 && new[0]==0))
	if(source[0]==0 || link[0]==0)
	{
		printf("you gotta give me two good paths, dude!\n");
		return 1;
	}

	// Set proper dev
	if(link[0] == '/')
		Ldev = root->dev;
	else
		Ldev = running->cwd->dev;

	// Copy pathname so it can be altered
	strcpy(pathCpy1, link);
	strcpy(pathCpy2, link);

	// Get parent and child pathnames
	strcpy(linkParentPath, dirname(pathCpy1));
	strcpy(linkChildPath, basename(pathCpy2));
	
	//###################
	symIno = my_creat(link); // CREATE FILE (holds the path for symbolic link)
	//###################
	
	lnkPIno = getino(fd, linkParentPath);

	symip = iget(fd, symIno);
	pmip = iget(fd, lnkPIno);

	pmip->dirty = 1;
	pmip->refCount++;
	pmip->INODE.i_atime = time(0L);

	iput(pmip);

	symip->dirty = 1;
	symip->refCount++;
	symip->INODE.i_links_count++;
	symip->INODE.i_mode = 0xA1A4; // ISLNK == true
	symip->INODE.i_size = strlen(source); // Entry is merely the size of the path it points to
	
	printf("Source: %s size: %d\n", source, strlen(source));

	//################### Copy the path to the the source file to symlink's block
	memcpy(symip->INODE.i_block, source, strlen(source)); 
	//###################

	
	iput(symip);
	return;
}




// CHMOD AND TOUCH

int my_chmod(char *mode, char *pathname)
{
	// NOTE: in input, mode comes from the usual pathname
	// buffer, while the pathname comes from buffer called
	// "thd". This keeps existing functions in order
	// while allowing for a third parameter in cases like this
	printf("Mode: %s\t", mode);
	printf("Path: %s\n", pathname);

	MINODE *mip;
	int modeCode, tmp, ino, dev = root->dev;

	// Make sure you have sufficient info
	if (!(mode[0] && pathname[0]))
	{
		printf("bad input\n");
		return;
	}

	// Get code from string
	sscanf(mode, "%x", &tmp);

	//printf("MODE CODE: %x\n", modeCode);


	ino = getino(dev, pathname);

	// As alawys, make sure getino returned something useful
	if (ino == 0)
		return 1;

	mip = iget(dev, ino);

	printf("MODE: %d\n", mip->INODE.i_mode);

	// Change mode and mark to be written back

	// Do some octal math
	modeCode = tmp << 6;
	modeCode |= tmp << 3;
	modeCode |= tmp;

	// OR that thing in there
	mip->INODE.i_mode |= modeCode;
	mip->dirty = 1;

	iput(mip);
	return;
}




int my_touch(char *pathname)
{
	MINODE *mip;
	int ino;

	if (pathname[0] == 0)
	{
		printf("bad path, can't touch this!\n");
		return;
	}

	ino = getino(fd, pathname);
	mip = iget(fd, ino);

	// MUST SET THEM TOGETHER SO TIME IS EXACTLY THE SAME
	mip->INODE.i_atime = mip->INODE.i_mtime = time(0L);
	mip->dirty = 1;

	iput(mip);
	return;
}


// Miscellaneous Subroutines


// Receives parent MINODE and child name to be deleted from said MINODE
int deleteChild (MINODE *pip, char *childname)
{
	char buf[BLKSIZE], *cp, *end;
	DIR *dp;
	int tmp, i = 0, last;

	last = 0;
	i = 0;

	get_block(fd, pip->INODE.i_block[0], buf);

	cp = buf;
	end = buf;
	dp = (DIR *)buf;

	// Loop end through all dir pointers until last entry found
	while (end + dp->rec_len < buf + BLKSIZE)
	{
		end += dp->rec_len;
		dp = (DIR *)end;
	}

	// set dp back to start to start search
	dp = (DIR *)cp;

	while (cp < buf + BLKSIZE)
	{
		// If the current child's namelen and childName len are same,
		// we may have found the proper child
		if (dp->name_len == strlen(childname))
		{
			// Now check if literal names match
			if (strncmp(childname, dp->name, dp->name_len) == 0)
			{
				// Match, so record current record length
				tmp = dp->rec_len;

				// If match is the last entry in the minode,
				// put dp back to the previous entry and extend
				// it's rec_len to cover to-be-deleted 
				if (cp == end)
				{
					dp = (DIR *)last;
					dp->rec_len += tmp;
					break;
				}
				else
				{
					dp = (DIR *)end;
					dp->rec_len += tmp;
					memcpy(cp, cp+tmp, BLKSIZE-i-tmp);
				}
				break;
			}
		}
		// On loop: store hard pointer to previous dp
		// increment total record length iterated over
		last = (int)cp;
		i += dp->rec_len;
		cp += dp->rec_len;
		dp = (DIR *)cp;
	}

	put_block(fd, pip->INODE.i_block[0], buf);

	return 0;
}






// Inode, blocks, inode blocks, custodial functions, etc.

int tst_bit(char *buf, int bit)
{
  int i, j;

  i = bit / 8;
  j = bit % 8;
  if (buf[i] & (1 << j)){
    return 1;
  }
  return 0;
}

int clr_bit(char *buf, int bit)
{
  int i, j;
  i = bit / 8;
  j = bit % 8;
  buf[i] &= ~(1 << j);
  return 0;
}

int set_bit(char *buf, int bit)
{
  int i, j;
  i = bit / 8;
  j = bit % 8;
  buf[i] |= (1 << j);
  return 0;
}

int incFreeInodes(int dev)
{
  char buf[BLKSIZE];

  // inc free inodes count in SUPER and GD
  get_block(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_inodes_count++;
  put_block(dev, 1, buf);

  get_block(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_inodes_count++;
  put_block(dev, 2, buf);
}

int decFreeInodes(int dev)
{
  char buf[BLKSIZE];

  // inc free inodes count in SUPER and GD
  get_block(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_inodes_count--;
  put_block(dev, 1, buf);

  get_block(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_inodes_count--;
  put_block(dev, 2, buf);
}

int incFreeBlocks(int dev)
{
  char buf[BLKSIZE];

  // inc free block count in SUPER and GD
  get_block(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_blocks_count++;
  put_block(dev, 1, buf);

  get_block(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_blocks_count++;
  put_block(dev, 2, buf);
}

int decFreeBlocks(int dev)
{
  char buf[BLKSIZE];

  // inc free block count in SUPER and GD
  get_block(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_blocks_count--;
  put_block(dev, 1, buf);

  get_block(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_blocks_count--;
  put_block(dev, 2, buf);
}


unsigned long ialloc(int dev)
{
	int i;
	char buf[BLKSIZE];

	printf("IALLOC!\n");

	// get inode Bitmap into buf
	get_block(dev, imap, buf);
	 
	for (i=0; i < ninodes; i++){
	  if (tst_bit(buf, i)==0){
	    set_bit(buf, i);
	    put_block(dev, imap, buf);

	    // update free inode count in SUPER and GD
	    decFreeInodes(dev);
	     
	    printf("ialloc: ino=%d\n", i+1);
	    return (i+1);
	  }
	}
	return 0;
} 

idealloc(int dev, int ino)
{
  int i;  
  char buf[BLKSIZE];

  if (ino > ninodes){
    printf("inumber %d out of range\n", ino);
    return;
  }

  // get inode bitmap block
  get_block(dev, imap, buf);
  clr_bit(buf, ino-1);

  // write buf back
  put_block(dev, imap, buf);

  // update free inode count in SUPER and GD
  incFreeInodes(dev);
}

unsigned long balloc(int dev)
{
  	int i;
	char buf[BLKSIZE];

	// get inode Bitmap into buf
	get_block(dev, bmap, buf);
	 
	for (i=0; i < nblocks; i++){
	  if (tst_bit(buf, i)==0){
	    set_bit(buf, i);
	    put_block(dev, bmap, buf);

	    // update free inode count in SUPER and GD
	    decFreeBlocks(dev);
	     
	    printf("balloc: block=%d\n", i+1);
	    return (i+1);
	  }
	}
	return 0;
}


int bdealloc(int dev, int bit)
{
  int i;  
  char buf[BLKSIZE];

  if (bit > nblocks){
    printf("bit %d out of range\n", bit);
    return;
  }

  // get inode bitmap block
  get_block(dev, imap, buf);
  clr_bit(buf, bit-1);

  // write buf back
  put_block(dev, bmap, buf);

  // update free inode count in SUPER and GD
  incFreeBlocks(dev);
}

int trash_iblocks(int fd, MINODE *tmip)
{
	char buf1[BLKSIZE], buf2[BLKSIZE], dblBuf[BLKSIZE];
	int i =0, j = 0, oneBlk, twoBlk;
	unsigned long *ind, *dind;


	// Get bitmap
	get_block(fd, bmap, buf1);

	// Loop through direct blocks of tmip
	for (i = 0; i < 12; i++)
	{
		// block needs to be cleared
		if (tmip->INODE.i_block[i] != 0)
		{
			clr_bit(buf1, tmip->INODE.i_block[i]-1);
			tmip->INODE.i_block[i] = 0;
		}
		else // reached end of used blocks, put it back
		{
			put_block(fd, bmap, buf1);
			return;
		}
	}//good

	// If all direct blocks were nonzero, i should now
	// be == 12, aka first indirect block
	if (tmip->INODE.i_block[i] != 0)
	{
		oneBlk = tmip->INODE.i_block[i];
		get_block(fd, oneBlk, buf2);
		ind = (unsigned long *)buf2;

		// Clear all those bits
		for (i = 0; i < 256; i++)
		{
			if (*ind != 0)
			{
				clr_bit(buf1, *ind-1);
				*ind = 0;
				ind++;
			}
			else
			{
				clr_bit(buf1, oneBlk-1);
				put_block(fd, oneBlk, buf2);
				put_block(fd, bmap, buf1);
				tmip->INODE.i_block[12] = 0;
				return;//10
			}

		}//good
	}
	else // We're done after all
	{
		put_block(fd, bmap, buf1);
		return;
	}

	// Handle potential double indirect situations
	if (tmip->INODE.i_block[13] != 0)
	{
		twoBlk = tmip->INODE.i_block[13];
		get_block(fd, twoBlk, dblBuf);
		dind = (unsigned int *)dblBuf;

		// Nested loop to clear each indirect block
		// within the double indirect blocks
		for (i = 0; i < 256; i++)
		{
			oneBlk = *dind;
			get_block(fd, oneBlk, buf2);
			ind = (unsigned long *)buf2;

			for (j = 0; j < 256; j++)
			{
				if (*ind != 0)
				{
					clr_bit(buf1, *ind-1);
					*ind = 0;
					ind++;
				}
				else // end of occupied blocks found
				{
					clr_bit(buf1, oneBlk-1);
					clr_bit(buf1, twoBlk-1);
					put_block(fd, oneBlk, buf2);
					put_block(fd, bmap, buf1);
					put_block(fd, twoBlk, dblBuf);
					tmip->INODE.i_block[13] = 0;
					return;
				}
				clr_bit(buf1, oneBlk-1);
			}

			dind++; // Increment block pointer

			if (*dind == 0)
			{
				clr_bit(buf1, oneBlk-1);
				clr_bit(buf1, twoBlk-1);
				put_block(fd, oneBlk, buf2);
				put_block(fd, bmap, buf1);
				put_block(fd, twoBlk, dblBuf);
				tmip->INODE.i_block[13] = 0;
				return;
			}

		}
	}
	else
	{
		put_block(fd, bmap, buf1);
		return;
	}
}

// Empty out an MINODE and mark its latest access time/need to be stored
int reduce(int fd, MINODE *rmip)
{
	trash_iblocks(fd, rmip);
	rmip->INODE.i_size = 0;
	rmip->INODE.i_atime = time(0L);
	rmip->INODE.i_mtime = time(0L);
	rmip->dirty = 1;
}





int quit()
{
	int i;
	MINODE *mip;
	for (i = 0; i < NMINODES; i++)
	{
		mip = &minode[i];
		if (mip->refCount > 0)
			iput(mip);
	}

	exit(0);
}