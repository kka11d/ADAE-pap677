#include <petscconf.h>
// We need to define this ahead of any other includes to make sure mkstemp is actually defined
#if defined(PETSC_HAVE_MKSTEMP)
  #define _XOPEN_SOURCE 600
#endif
#include "petsc/private/petscimpl.h"
#include "petscerror.h"
#include "petscis.h"
#include "petscstring.h"
#include "petscsys.h"
#include "petscsystypes.h"
#include <petsc/private/dmnetworkimpl.h> /*I  "petscdmnetwork.h"  I*/
#include <petscdraw.h>

static PetscErrorCode DMView_Network_CSV(DM dm, PetscViewer viewer)
{
  DM              dmcoords;
  PetscInt        nsubnets, i, subnet, nvertices, nedges, vertex, edge;
  PetscInt        vertexOffsets[2], globalEdgeVertices[2];
  PetscScalar     vertexCoords[2];
  const PetscInt *vertices, *edges, *edgeVertices;
  Vec             allVertexCoords;
  PetscMPIInt     rank;
  MPI_Comm        comm;

  PetscFunctionBegin;
  // Get the network containing coordinate information
  PetscCall(DMGetCoordinateDM(dm, &dmcoords));
  // Get the coordinate vector for the network
  PetscCall(DMGetCoordinatesLocal(dm, &allVertexCoords));
  // Get the MPI communicator and this process' rank
  PetscCall(PetscObjectGetComm((PetscObject)dm, &comm));
  PetscCallMPI(MPI_Comm_rank(comm, &rank));
  // Start synchronized printing
  PetscCall(PetscViewerASCIIPushSynchronized(viewer));

  // Write the header
  PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "Type,Rank,ID,X,Y,Z,Name,Color\n"));

  // Iterate each subnetwork (Note: We need to get the global number of subnets apparently)
  PetscCall(DMNetworkGetNumSubNetworks(dm, NULL, &nsubnets));
  for (subnet = 0; subnet < nsubnets; subnet++) {
    // Get the subnetwork's vertices and edges
    PetscCall(DMNetworkGetSubnetwork(dm, subnet, &nvertices, &nedges, &vertices, &edges));

    // Write out each vertex
    for (i = 0; i < nvertices; i++) {
      vertex = vertices[i];
      // Get the offset into the coordinate vector for the vertex
      PetscCall(DMNetworkGetLocalVecOffset(dmcoords, vertex, ALL_COMPONENTS, vertexOffsets));
      vertexOffsets[1] = vertexOffsets[0] + 1;
      // Remap vertex to the global value
      PetscCall(DMNetworkGetGlobalVertexIndex(dm, vertex, &vertex));
      // Get the vertex position from the coordinate vector
      PetscCall(VecGetValues(allVertexCoords, 2, vertexOffsets, vertexCoords));

      // TODO: Determine vertex color/name
      PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "Node,%" PetscInt_FMT ",%" PetscInt_FMT ",%lf,%lf,0,%" PetscInt_FMT "\n", (PetscInt)rank, vertex, (double)PetscRealPart(vertexCoords[0]), (double)PetscRealPart(vertexCoords[1]), vertex));
    }

    // Write out each edge
    for (i = 0; i < nedges; i++) {
      edge = edges[i];
      PetscCall(DMNetworkGetConnectedVertices(dm, edge, &edgeVertices));
      PetscCall(DMNetworkGetGlobalVertexIndex(dm, edgeVertices[0], &globalEdgeVertices[0]));
      PetscCall(DMNetworkGetGlobalVertexIndex(dm, edgeVertices[1], &globalEdgeVertices[1]));
      PetscCall(DMNetworkGetGlobalEdgeIndex(dm, edge, &edge));

      // TODO: Determine edge color/name
      PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "Edge,%" PetscInt_FMT ",%" PetscInt_FMT ",%" PetscInt_FMT ",%" PetscInt_FMT ",0,%" PetscInt_FMT "\n", (PetscInt)rank, edge, globalEdgeVertices[0], globalEdgeVertices[1], edge));
    }
  }
  // End synchronized printing
  PetscCall(PetscViewerFlush(viewer));
  PetscCall(PetscViewerASCIIPopSynchronized(viewer));
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode DMView_Network_Matplotlib(DM dm, PetscViewer viewer)
{
  PetscMPIInt rank, size;
  MPI_Comm    comm;
  char        filename[PETSC_MAX_PATH_LEN + 1], options[512], proccall[PETSC_MAX_PATH_LEN + 512], scriptFile[PETSC_MAX_PATH_LEN + 1], buffer[256];
  PetscViewer csvViewer;
  FILE       *processFile = NULL;
  PetscBool   isnull, optionShowRanks = PETSC_FALSE, optionRankIsSet = PETSC_FALSE, showNoNodes = PETSC_FALSE, showNoNumbering = PETSC_FALSE;
  PetscDraw   draw;
  DM_Network *network = (DM_Network *)dm->data;
  PetscReal   drawPause;
  PetscInt    i;
#if defined(PETSC_HAVE_MKSTEMP)
  PetscBool isSharedTmp;
#endif

  PetscFunctionBegin;
  // Deal with the PetscDraw we are given
  PetscCall(PetscViewerDrawGetDraw(viewer, 1, &draw));
  PetscCall(PetscDrawIsNull(draw, &isnull));
  PetscCall(PetscDrawSetVisible(draw, PETSC_FALSE));

  // Clear the file name buffer so all communicated bytes are well-defined
  PetscCall(PetscMemzero(filename, sizeof(filename)));

  // Get the MPI communicator and this process' rank
  PetscCall(PetscObjectGetComm((PetscObject)dm, &comm));
  PetscCallMPI(MPI_Comm_rank(comm, &rank));
  PetscCallMPI(MPI_Comm_size(comm, &size));

#if defined(PETSC_HAVE_MKSTEMP)
  // Get if the temporary directory is shared
  // Note: This must be done collectively on every rank, it cannot be done on a single rank
  PetscCall(PetscSharedTmp(comm, &isSharedTmp));
#endif

  /* Process Options */
  optionShowRanks = network->vieweroptions.showallranks;
  showNoNodes     = network->vieweroptions.shownovertices;
  showNoNumbering = network->vieweroptions.shownonumbering;

  /*
    TODO:  if the option -dmnetwork_view_tmpdir can be moved up here that would be good as well.
  */
  PetscOptionsBegin(PetscObjectComm((PetscObject)dm), ((PetscObject)dm)->prefix, "MatPlotLib PetscViewer DMNetwork Options", "PetscViewer");
  PetscCall(PetscOptionsBool("-dmnetwork_view_all_ranks", "View all ranks in the DMNetwork", NULL, optionShowRanks, &optionShowRanks, NULL));
  PetscCall(PetscOptionsString("-dmnetwork_view_rank_range", "Set of ranks to view the DMNetwork on", NULL, buffer, buffer, sizeof(buffer), &optionRankIsSet));
  PetscCall(PetscOptionsBool("-dmnetwork_view_no_vertices", "Do not view vertices", NULL, showNoNodes, &showNoNodes, NULL));
  PetscCall(PetscOptionsBool("-dmnetwork_view_no_numbering", "Do not view edge and vertex numbering", NULL, showNoNumbering, &showNoNumbering, NULL));
  PetscOptionsEnd();

  // Generate and broadcast the temporary file name from rank 0
  if (rank == 0) {
#if defined(PETSC_HAVE_TMPNAM_S)
    // Acquire a temporary file to write to and open an ASCII/CSV viewer
    PetscCheck(tmpnam_s(filename, sizeof(filename)) == 0, comm, PETSC_ERR_SYS, "Could not acquire temporary file");
#elif defined(PETSC_HAVE_MKSTEMP)
    PetscBool isTmpOverridden;
    size_t    numChars;
    // Same thing, but for POSIX systems on which tmpnam is deprecated
    // Note: Configure may detect mkstemp but it will not be defined if compiling for C99, so check additional defines to see if we can use it
    // Mkstemp requires us to explicitly specify part of the path, but some systems may not like putting files in /tmp/ so have an option for it
    PetscCall(PetscOptionsGetString(NULL, NULL, "-dmnetwork_view_tmpdir", filename, sizeof(filename), &isTmpOverridden));
    // If not specified by option try using a shared tmp on the system
    if (!isTmpOverridden) {
      // Validate that if tmp is not overridden it is at least shared
      PetscCheck(isSharedTmp, comm, PETSC_ERR_SUP_SYS, "Temporary file directory is not shared between ranks, try using -dmnetwork_view_tmpdir to specify a shared directory");
      PetscCall(PetscGetTmp(PETSC_COMM_SELF, filename, sizeof(filename)));
    }
    // Make sure the filename ends with a '/'
    PetscCall(PetscStrlen(filename, &numChars));
    if (filename[numChars - 1] != '/') {
      filename[numChars]     = '/';
      filename[numChars + 1] = 0;
    }
    // Perform the actual temporary file creation
    PetscCall(PetscStrlcat(filename, "XXXXXX", sizeof(filename)));
    PetscCheck(mkstemp(filename) != -1, comm, PETSC_ERR_SYS, "Could not acquire temporary file");
#else
    // Same thing, but for older C versions which don't have the safe form
    PetscCheck(tmpnam(filename) != NULL, comm, PETSC_ERR_SYS, "Could not acquire temporary file");
#endif
  }

  // Broadcast the filename to all other MPI ranks
  PetscCallMPI(MPI_Bcast(filename, PETSC_MAX_PATH_LEN, MPI_BYTE, 0, comm));

  PetscCall(PetscViewerASCIIOpen(comm, filename, &csvViewer));
  PetscCall(PetscViewerPushFormat(csvViewer, PETSC_VIEWER_ASCII_CSV));

  // Use the CSV viewer to write out the local network
  PetscCall(DMView_Network_CSV(dm, csvViewer));

  // Close the viewer
  PetscCall(PetscViewerDestroy(&csvViewer));

  // Generate options string
  PetscCall(PetscMemzero(options, sizeof(options)));
  // If the draw is null run as a "test execute" ie. do nothing just test that the script was called correctly
  PetscCall(PetscStrlcat(options, isnull ? " -tx " : " ", sizeof(options)));
  PetscCall(PetscDrawGetPause(draw, &drawPause));
  if (drawPause > 0) {
    char pausebuffer[64];
    PetscCall(PetscSNPrintf(pausebuffer, sizeof(pausebuffer), "%f", (double)drawPause));
    PetscCall(PetscStrlcat(options, " -dt ", sizeof(options)));
    PetscCall(PetscStrlcat(options, pausebuffer, sizeof(options)));
  }
  if (optionShowRanks || optionRankIsSet) {
    // Show all ranks only if the option is set in code or by the user AND not showing specific ranks AND there is more than one process
    if (optionShowRanks && !optionRankIsSet && size != 1) PetscCall(PetscStrlcat(options, " -dar ", sizeof(options)));
    // Do not show the global plot if the user requests it OR if one specific rank is requested
    if (network->vieweroptions.dontshowglobal || optionRankIsSet) PetscCall(PetscStrlcat(options, " -ncp ", sizeof(options)));

    if (optionRankIsSet) {
      // If a range of ranks to draw is specified append it
      PetscCall(PetscStrlcat(options, " -drr ", sizeof(options)));
      PetscCall(PetscStrlcat(options, buffer, sizeof(options)));
    } else {
      // Otherwise, use the options provided in code
      if (network->vieweroptions.viewranks) {
        const PetscInt *viewranks;
        PetscInt        viewrankssize;
        char            rankbuffer[64];
        PetscCall(ISGetTotalIndices(network->vieweroptions.viewranks, &viewranks));
        PetscCall(ISGetSize(network->vieweroptions.viewranks, &viewrankssize));
        PetscCall(PetscStrlcat(options, " -drr ", sizeof(options)));
        for (i = 0; i < viewrankssize; i++) {
          PetscCall(PetscSNPrintf(rankbuffer, sizeof(rankbuffer), "%" PetscInt_FMT, viewranks[i]));
          PetscCall(PetscStrlcat(options, rankbuffer, sizeof(options)));
        }
        PetscCall(ISRestoreTotalIndices(network->vieweroptions.viewranks, &viewranks));
      } // if not provided an IS of viewing ranks, skip viewing
    }
  }

  // Check for options for visibility...
  if (showNoNodes) PetscCall(PetscStrlcat(options, " -nn ", sizeof(options)));
  if (showNoNumbering) PetscCall(PetscStrlcat(options, " -nnl -nel ", sizeof(options)));

  // Get the value of $PETSC_DIR
  PetscCall(PetscStrreplace(comm, "${PETSC_DIR}/share/petsc/bin/dmnetwork_view.py", scriptFile, sizeof(scriptFile)));
  PetscCall(PetscFixFilename(scriptFile, scriptFile));
  // Generate the system call for 'python3 $PETSC_DIR/share/petsc/dmnetwork_view.py <options> <file>'
  PetscCall(PetscArrayzero(proccall, sizeof(proccall)));
  PetscCall(PetscSNPrintf(proccall, sizeof(proccall), "%s %s %s %s", PETSC_PYTHON_EXE, scriptFile, options, filename));

#if defined(PETSC_HAVE_POPEN)
  // Perform the call to run the python script (Note: while this is called on all ranks POpen will only run on rank 0)
  PetscCall(PetscPOpen(comm, NULL, proccall, "r", &processFile));
  if (processFile != NULL) {
    while (fgets(buffer, sizeof(buffer), processFile) != NULL) PetscCall(PetscPrintf(comm, "%s", buffer));
  }
  PetscCall(PetscPClose(comm, processFile));
#else
  // Same thing, but using the standard library for systems that don't have POpen/PClose (only run on rank 0)
  if (rank == 0) PetscCheck(system(proccall) == 0, PETSC_COMM_SELF, PETSC_ERR_SYS, "Failed to call viewer script");
  // Barrier so that all ranks wait until the call completes
  PetscCallMPI(MPI_Barrier(comm));
#endif
  // Clean up the temporary file we used using rank 0
  if (rank == 0) PetscCheck(remove(filename) == 0, PETSC_COMM_SELF, PETSC_ERR_SYS, "Failed to delete temporary file");
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode DMView_Network(DM dm, PetscViewer viewer)
{
  PetscBool         iascii, isdraw;
  PetscViewerFormat format;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidHeaderSpecific(viewer, PETSC_VIEWER_CLASSID, 2);
  PetscCall(PetscViewerGetFormat(viewer, &format));

  PetscCall(PetscObjectTypeCompare((PetscObject)viewer, PETSCVIEWERDRAW, &isdraw));
  if (isdraw) {
    PetscCall(DMView_Network_Matplotlib(dm, viewer));
    PetscFunctionReturn(PETSC_SUCCESS);
  }

  PetscCall(PetscObjectTypeCompare((PetscObject)viewer, PETSCVIEWERASCII, &iascii));
  if (iascii) {
    const PetscInt *cone, *vtx, *edges;
    PetscInt        vfrom, vto, i, j, nv, ne, nsv, p, nsubnet;
    DM_Network     *network = (DM_Network *)dm->data;
    PetscMPIInt     rank;

    PetscCallMPI(MPI_Comm_rank(PetscObjectComm((PetscObject)dm), &rank));
    if (format == PETSC_VIEWER_ASCII_CSV) {
      PetscCall(DMView_Network_CSV(dm, viewer));
      PetscFunctionReturn(PETSC_SUCCESS);
    }

    nsubnet = network->cloneshared->Nsubnet; /* num of subnetworks */
    if (!rank) {
      PetscCall(PetscPrintf(PETSC_COMM_SELF, "  NSubnets: %" PetscInt_FMT "; NEdges: %" PetscInt_FMT "; NVertices: %" PetscInt_FMT "; NSharedVertices: %" PetscInt_FMT ".\n", nsubnet, network->cloneshared->NEdges, network->cloneshared->NVertices,
                            network->cloneshared->Nsvtx));
    }

    PetscCall(DMNetworkGetSharedVertices(dm, &nsv, NULL));
    PetscCall(PetscViewerASCIIPushSynchronized(viewer));
    PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "  [%d] nEdges: %" PetscInt_FMT "; nVertices: %" PetscInt_FMT "; nSharedVertices: %" PetscInt_FMT "\n", rank, network->cloneshared->nEdges, network->cloneshared->nVertices, nsv));

    for (i = 0; i < nsubnet; i++) {
      PetscCall(DMNetworkGetSubnetwork(dm, i, &nv, &ne, &vtx, &edges));
      if (ne) {
        PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "     Subnet %" PetscInt_FMT ": nEdges %" PetscInt_FMT ", nVertices(include shared vertices) %" PetscInt_FMT "\n", i, ne, nv));
        for (j = 0; j < ne; j++) {
          p = edges[j];
          PetscCall(DMNetworkGetConnectedVertices(dm, p, &cone));
          PetscCall(DMNetworkGetGlobalVertexIndex(dm, cone[0], &vfrom));
          PetscCall(DMNetworkGetGlobalVertexIndex(dm, cone[1], &vto));
          PetscCall(DMNetworkGetGlobalEdgeIndex(dm, edges[j], &p));
          PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "       edge %" PetscInt_FMT ": %" PetscInt_FMT " ----> %" PetscInt_FMT "\n", p, vfrom, vto));
        }
      }
    }

    /* Shared vertices */
    PetscCall(DMNetworkGetSharedVertices(dm, NULL, &vtx));
    if (nsv) {
      PetscInt        gidx;
      PetscBool       ghost;
      const PetscInt *sv = NULL;

      PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "     SharedVertices:\n"));
      for (i = 0; i < nsv; i++) {
        PetscCall(DMNetworkIsGhostVertex(dm, vtx[i], &ghost));
        if (ghost) continue;

        PetscCall(DMNetworkSharedVertexGetInfo(dm, vtx[i], &gidx, &nv, &sv));
        PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "       svtx %" PetscInt_FMT ": global index %" PetscInt_FMT ", subnet[%" PetscInt_FMT "].%" PetscInt_FMT " ---->\n", i, gidx, sv[0], sv[1]));
        for (j = 1; j < nv; j++) PetscCall(PetscViewerASCIISynchronizedPrintf(viewer, "                                           ----> subnet[%" PetscInt_FMT "].%" PetscInt_FMT "\n", sv[2 * j], sv[2 * j + 1]));
      }
    }
    PetscCall(PetscViewerFlush(viewer));
    PetscCall(PetscViewerASCIIPopSynchronized(viewer));
  } else PetscCheck(iascii, PetscObjectComm((PetscObject)dm), PETSC_ERR_SUP, "Viewer type %s not yet supported for DMNetwork writing", ((PetscObject)viewer)->type_name);
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*@
  DMNetworkViewSetShowRanks - Sets viewing the `DMETNWORK` on each rank individually.

  Logically Collective

  Input Parameter:
. dm - the `DMNETWORK` object

  Output Parameter:
. showranks - `PETSC_TRUE` if viewing each rank's sub network individually

  Level: beginner

.seealso: `DM`, `DMNETWORK`, `DMNetworkViewSetShowGlobal()`, `DMNetworkViewSetShowVertices()`, `DMNetworkViewSetShowNumbering()`, `DMNetworkViewSetViewRanks()`
@*/
PetscErrorCode DMNetworkViewSetShowRanks(DM dm, PetscBool showranks)
{
  DM_Network *network = (DM_Network *)dm->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecificType(dm, DM_CLASSID, 1, DMNETWORK);
  network->vieweroptions.showallranks = showranks;
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*@
  DMNetworkViewSetShowGlobal - Set viewing the global network.

  Logically Collective

  Input Parameter:
. dm - the `DMNETWORK` object

  Output Parameter:
. showglobal - `PETSC_TRUE` if viewing the global network

  Level: beginner

.seealso: `DM`, `DMNETWORK`, `DMNetworkViewSetShowRanks()`, `DMNetworkViewSetShowVertices()`, `DMNetworkViewSetShowNumbering()`, `DMNetworkViewSetViewRanks()`
@*/
PetscErrorCode DMNetworkViewSetShowGlobal(DM dm, PetscBool showglobal)
{
  DM_Network *network = (DM_Network *)dm->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecificType(dm, DM_CLASSID, 1, DMNETWORK);
  network->vieweroptions.dontshowglobal = (PetscBool)(!showglobal);
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*@
  DMNetworkViewSetShowVertices - Sets whether to display the vertices in viewing routines.

  Logically Collective

  Input Parameter:
. dm - the `DMNETWORK` object

  Output Parameter:
. showvertices - `PETSC_TRUE` if visualizing the vertices

  Level: beginner

.seealso: `DM`, `DMNETWORK`, `DMNetworkViewSetShowRanks()`, `DMNetworkViewSetShowGlobal()`, `DMNetworkViewSetShowNumbering()`, `DMNetworkViewSetViewRanks()`
@*/
PetscErrorCode DMNetworkViewSetShowVertices(DM dm, PetscBool showvertices)
{
  DM_Network *network = (DM_Network *)dm->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecificType(dm, DM_CLASSID, 1, DMNETWORK);
  network->vieweroptions.shownovertices = (PetscBool)(!showvertices);
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*@
  DMNetworkViewSetShowNumbering - Set displaying the numbering of edges and vertices in viewing routines.

  Logically Collective

  Input Parameter:
. dm - the `DMNETWORK` object

  Output Parameter:
. shownumbering - `PETSC_TRUE` if displaying the numbering of edges and vertices

  Level: beginner

.seealso: `DM`, `DMNETWORK`, `DMNetworkViewSetShowRanks()`, `DMNetworkViewSetShowGlobal()`, `DMNetworkViewSetShowVertices()`, `DMNetworkViewSetViewRanks()`
@*/
PetscErrorCode DMNetworkViewSetShowNumbering(DM dm, PetscBool shownumbering)
{
  DM_Network *network = (DM_Network *)dm->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecificType(dm, DM_CLASSID, 1, DMNETWORK);
  network->vieweroptions.shownonumbering = (PetscBool)(!shownumbering);
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*@
  DMNetworkViewSetViewRanks - View the `DMNETWORK` on each of the specified ranks individually.

  Collective

  Input Parameter:
. dm - the `DMNETWORK` object

  Output Parameter:
. viewranks - set of ranks to view the `DMNETWORK` on individually

  Level: beginner

  Note:
  `DMNetwork` takes ownership of the input viewranks `IS`, it should be destroyed by the caller.

.seealso: `DM`, `DMNETWORK`, `DMNetworkViewSetShowRanks()`, `DMNetworkViewSetShowGlobal()`, `DMNetworkViewSetShowVertices()`, `DMNetworkViewSetShowNumbering()`
@*/
PetscErrorCode DMNetworkViewSetViewRanks(DM dm, IS viewranks)
{
  DM_Network *network = (DM_Network *)dm->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecificType(dm, DM_CLASSID, 1, DMNETWORK);
  PetscValidHeaderSpecific(viewranks, IS_CLASSID, 2);
  PetscCheckSameComm(dm, 1, viewranks, 2);
  PetscCall(ISDestroy(&network->vieweroptions.viewranks));
  PetscCall(PetscObjectReference((PetscObject)viewranks));
  network->vieweroptions.viewranks = viewranks;
  PetscFunctionReturn(PETSC_SUCCESS);
}
