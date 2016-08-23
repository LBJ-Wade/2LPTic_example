#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include <inttypes.h>
#include <limits.h>

#include "allvars.h"
#include "proto.h"
#include "save.h"
#include "read_glass.h"

int find_files(char *fname);

void read_glass(char *fname)
{
  int64_t i, j, k, n;
  int slab, count, type;
  uint32_t dummy, dummy2, m;
  float *pos = 0;
  float x, y, z;
  FILE *fd = 0;
  size_t bytes;
  int *npart_Task;
  int num, numfiles, skip, nlocal;
  char buf[500];

#define SKIP {my_fread(&dummy, sizeof(int), 1, fd);}
#define SKIP2 {my_fread(&dummy2, sizeof(int), 1, fd);}

  if(ThisTask == 0)
    {
      printf("\nreading Lagrangian glass file...\n");
      fflush(stdout);

      numfiles = find_files(fname);

      for(num = 0, skip = 0; num < numfiles; num++)
		{
		  if(numfiles > 1)
			sprintf(buf, "%s.%d", fname, num);
		  else
			sprintf(buf, "%s", fname);

		  fd = fopen(buf, "r");
		  if(fd == NULL)
			{
			  printf("can't open file `%s' for reading glass file.\n", buf);
			  FatalError(1);
			}

		  SKIP;
		  my_fread(&header1, sizeof(header1), 1, fd);
		  SKIP2;

		  if(dummy != sizeof(header1) || dummy2 != sizeof(header1))
			{
			  printf("incorrect header size!\n");
			  FatalError(2);
			}

		  nlocal = 0;

		  for(k = 0; k < 6; k++)
			nlocal += header1.npart[k];

		  printf("reading '%s' with %d particles\n", fname, nlocal);

		  if(num == 0)
			{
			  Nglass = 0;

			  for(k = 0; k < 6; k++)
				Nglass += header1.npartTotal[k];

			  printf("\nNglass= %d\n\n", Nglass);
			  pos = malloc(sizeof(*pos) * Nglass * 3);

			  if(pos == NULL)
				{
				  printf("failed to allocate %g Mbyte on Task %d for glass file\n",
						 sizeof(float) * Nglass * 3.0 / (1024.0 * 1024.0), ThisTask);
				  FatalError(112);
				}
			}

		  SKIP;
		  my_fread(&pos[3 * skip], sizeof(float), 3 * nlocal, fd);
		  SKIP2;

		  if(dummy != sizeof(float) * 3 * nlocal || dummy2 != sizeof(float) * 3 * nlocal)
			{
			  printf("incorrect block structure in positions block!\n");
			  FatalError(3);
			}
		  skip += nlocal;

		  fclose(fd);
		}
    }

  MPI_Bcast(&Nglass, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&header1, sizeof(header1), MPI_BYTE, 0, MPI_COMM_WORLD);

  if(ThisTask != 0)
    {
      pos = malloc(sizeof(*pos) * Nglass * 3);

      if(pos == NULL)
		{
		  printf("failed to allocate %g Mbyte on Task %d for glass file\n",
				 sizeof(float) * Nglass * 3.0 / (1024.0 * 1024.0), ThisTask);
		  FatalError(112);
		}
    }

  MPI_Bcast(&pos[0], sizeof(float) * Nglass * 3, MPI_BYTE, 0, MPI_COMM_WORLD);

  bytes = sizeof(int) * NTask;
  npart_Task = malloc(bytes);
  ASSERT_ALLOC(npart_Task);
  

  for(i = 0; i < NTask; i++)
    npart_Task[i] = 0;

#if defined(MULTICOMPONENTGLASSFILE) && defined(DIFFERENT_TRANSFER_FUNC)
  MinType = 7;
  MaxType = -2;
  for(type = 0; type < 6; type++)
    if(header1.npartTotal[type])
      {
		if(MinType > type - 1)
		  MinType = type - 1;

		if(MaxType < type - 1)
		  MaxType = type - 1;
      }
#endif

#if defined(PRODUCE_CONSISTENT_IDS)
  /* Code from Greg Poole to generate same IDs across different simulations. 
	 Makes matching halos trivial afterwards. */
  for(i = 0; i < GlassTileFac; i++){
    if(i%GlassTileFacSample==0){
	  for(type = 0, n = 0; type < 6; type++)
		{
		  for(m = 0; m < header1.npartTotal[type]; m++, n++)
			{
			  x = pos[3 * n] / header1.BoxSize * (Box / GlassTileFac) + i * (Box / GlassTileFac);
			  
			  slab = x / Box * Nmesh;
			  if(slab >= Nmesh)
				slab = Nmesh - 1;
			  
			  npart_Task[Slab_to_task[slab]] += 1;
			}
		}
	}
  }

  const int64_t sqr_GlassTileFac = GlassTileFac/GlassTileFacSample * GlassTileFac/GlassTileFacSample;
  for(i=0;i<NTask;i++) {
	const int64_t this_npart = npart_Task[i];
	if(this_npart * sqr_GlassTileFac > INT_MAX) {
	  printf("On Task %d number of particles = %"PRId64" exceeds INT_MAX. Please increase the number of cpus (currently running on %d cpus) and rerun..aborting\n",
			 ThisTask, this_npart * sqr_GlassTileFac, NTask);
	  FatalError(3142);
	}
	npart_Task[i] = this_npart;
  }
  
#else

  /* Old code from standard 2LPTic */
  for(i = 0; i < GlassTileFac; i++)
	{
	  for(type = 0, n = 0; type < 6; type++)
		{
		  for(m = 0; m < header1.npartTotal[type]; m++, n++)
			{
			  x = pos[3 * n] / header1.BoxSize * (Box / GlassTileFac) + i * (Box / GlassTileFac);
			  
			  slab = x / Box * Nmesh;
			  if(slab >= Nmesh)
				slab = Nmesh - 1;
			  
			  npart_Task[Slab_to_task[slab]] += 1;
			}
		}
	}

  const int64_t sqr_GlassTileFac = GlassTileFac * GlassTileFac;
  for(i=0;i<NTask;i++) {
	const int64_t this_npart = npart_Task[i];
	if(this_npart * sqr_GlassTileFac > INT_MAX) {
	  printf("On Task %d number of particles = %"PRId64" exceeds INT_MAX. Please increase the number of cpus and rerun..aborting\n",
			 ThisTask, this_npart * sqr_GlassTileFac);
	  FatalError(3143);
	}
	npart_Task[i] = this_npart;
  }

#endif


  TotNumPart = 0;		/* note: This is a 64 bit integer */
  NTaskWithN = 0;

  NumPart = npart_Task[ThisTask];

#if defined(PRODUCE_CONSISTENT_IDS)
  /* Code from Greg Poole to generate consistent IDs across
	 simulations of different resolutions */
  ThisTaskFileNumber=0;
  for(i = 0; i < NTask; i++)
    {
      TotNumPart += npart_Task[i];
      if(npart_Task[i] > 0){
		NTaskWithN++;
        if(i<ThisTask && npart_Task[i]>0)
		  ThisTaskFileNumber++;
      }
    }
  if(npart_Task[ThisTask]<=0)
	ThisTaskFileNumber=-1;

#else 
  /* alternate code from old 2LPTic - does not preserve IDs */
  for(i = 0; i < NTask; i++)
    {
      TotNumPart += npart_Task[i];
      if(npart_Task[i] > 0)
		NTaskWithN++;
    }
#endif

  if(ThisTask == 0)
    {
      for(i = 0; i < NTask; i++)
		printf("%d particles on task=%"PRId64"  (slabs=%d)\n", npart_Task[i], i, Local_nx_table[i]);

      printf("\nTotal number of particles  = %d%09d\n\n",
			 (int) (TotNumPart / 1000000000), (int) (TotNumPart % 1000000000));

      fflush(stdout);
    }


  free(npart_Task);


  if(NumPart)
    {
	  bytes = sizeof(struct part_data) * NumPart;
      P = (struct part_data *) malloc(bytes);
      if(P == NULL)
		{
		  printf("failed to allocate %g Mbyte (%d particles) on Task %d\n", bytes / (1024.0 * 1024.0),
				 NumPart, ThisTask);
		  FatalError(9891);
		}
    }


  count = 0;

#if defined(PRODUCE_CONSISTENT_IDS)
  /* Code from Greg Poole to generate consistent particle IDs 
   across simulations of different resolutions */
  long long GTF1=(long long)GlassTileFac;
  long long GTF2=(long long)GlassTileFac*(long long)GlassTileFac;

  for(i = 0; i < GlassTileFac; i++){
    if(i%GlassTileFacSample==0){
	  for(j = 0; j < GlassTileFac; j++){
		if(j%GlassTileFacSample==0){
		  for(k = 0; k < GlassTileFac; k++){
			if(k%GlassTileFacSample==0){
			  for(type = 0, n = 0; type < 6; type++)
				{
				  for(m = 0; m < header1.npartTotal[type]; m++, n++)
					{
					  x = pos[3 * n] / header1.BoxSize * (Box / GlassTileFac) + i * (Box / GlassTileFac);
					  
					  slab = x / Box * Nmesh;
					  if(slab >= Nmesh)
						slab = Nmesh - 1;
					  
					  if(Slab_to_task[slab] == ThisTask)
						{
						  y = pos[3 * n + 1] / header1.BoxSize * (Box / GlassTileFac) + j * (Box / GlassTileFac);
						  z = pos[3 * n + 2] / header1.BoxSize * (Box / GlassTileFac) + k * (Box / GlassTileFac);
						  
						  P[count].Pos[0] = x;
						  P[count].Pos[1] = y;
						  P[count].Pos[2] = z;
#ifdef  MULTICOMPONENTGLASSFILE
						  P[count].Type = type - 1;
#endif
						  P[count].ID = 1+(((long long)i)*GTF2+((long long)j)*GTF1)+((long long)k);
#ifndef USE_64BITID
						  /*If 32bit IDs are requested, then check that these generated IDs will fit.
							Simulations are wasted no more. */
						  if(P[count].ID > INT_MAX) {
							printf("On task %d: Can not represent particles ID = %lld with 32 bit integers. Enabled the compile time options USE_64BITID\n", 
								   ThisTask, P[count].ID);
							FatalError(10000);
						  }
#endif
						  
						  count++;
						}
					}
				}
			}
		  }
		}
	  }
	}
  }

#else  //else for PRODUCE_CONSISTENT_IDS

  /* Alternate code from old 2LPTic */
  IDStart = 1;
  for(i = 0; i < GlassTileFac; i++)
    for(j = 0; j < GlassTileFac; j++)
      for(k = 0; k < GlassTileFac; k++)
		{
		  for(type = 0, n = 0; type < 6; type++)
			{
			  for(m = 0; m < header1.npartTotal[type]; m++, n++)
				{
				  x = pos[3 * n] / header1.BoxSize * (Box / GlassTileFac) + i * (Box / GlassTileFac);
				  
				  slab = x / Box * Nmesh;
				  if(slab >= Nmesh)
					slab = Nmesh - 1;
				  
				  if(Slab_to_task[slab] == ThisTask)
					{
					  y = pos[3 * n + 1] / header1.BoxSize * (Box / GlassTileFac) + j * (Box / GlassTileFac);
					  z = pos[3 * n + 2] / header1.BoxSize * (Box / GlassTileFac) + k * (Box / GlassTileFac);
					  
					  P[count].Pos[0] = x;
					  P[count].Pos[1] = y;
					  P[count].Pos[2] = z;
#ifdef  MULTICOMPONENTGLASSFILE
					  P[count].Type = type - 1;
#endif
					  
					  P[count].ID = IDStart;
#ifndef USE_64BITID
					  /*If 32bit IDs are requested, then check that these generated IDs will fit.
						Simulations are wasted no more. */
					  if(P[count].ID > INT_MAX) {
						printf("On task %d: Can not represent particles ID = %lld with 32 bit integers. Enabled the compile time options USE_64BITID\n", 
							   ThisTask, P[count].ID);
						FatalError(10000);
					  }
#endif
					  count++;
					}
				  
				  IDStart++;
				}
			}
		}
#endif //end of #if 0 -> alternate code for non-preserving IDs from original 2LPTic

  
  if(count != NumPart)
    {
      printf("fatal mismatch (%d %d) on Task %d\n", count, NumPart, ThisTask);
      FatalError(1);
    }

  free(pos);
}


int find_files(char *fname)
{
  FILE *fd;
  char buf[200], buf1[200];
  int32_t dummy;

  sprintf(buf, "%s.%d", fname, 0);
  sprintf(buf1, "%s", fname);

  if((fd = fopen(buf, "r")))
    {
      my_fread(&dummy, sizeof(dummy), 1, fd);
      my_fread(&header, sizeof(header), 1, fd);
      my_fread(&dummy, sizeof(dummy), 1, fd);

      fclose(fd);

      return header.num_files;
    }

  if((fd = fopen(buf1, "r")))
    {
      my_fread(&dummy, sizeof(dummy), 1, fd);
      my_fread(&header, sizeof(header), 1, fd);
      my_fread(&dummy, sizeof(dummy), 1, fd);

      fclose(fd);
      header.num_files = 1;

      return header.num_files;
    }

  FatalError(121);
  return 0;
}
