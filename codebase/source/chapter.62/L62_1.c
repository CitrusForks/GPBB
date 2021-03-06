/* Core renderer for Win32 program to demonstrate drawing from a 2D
   BSP tree; illustrate the use of BSP trees for surface visibility.
   UpdateWorld() is the top-level function in this module.
   Full source code for the BSP-based renderer, and for the
   accompanying BSP compiler, may be downloaded from
   ftp.idsoftware.com/mikeab, in the file ddjbsp2.zip.
   Tested with VC++ 2.0 running on Windows NT 3.5. */
#define FIXEDPOINT(x)   ((FIXEDPOINT)(((long)x)*((long)0x10000)))
#define FIXTOINT(x)     ((int)(x >> 16))
#define ANGLE(x)        ((long)x)
#define STANDARD_SPEED  (FIXEDPOINT(20))
#define STANDARD_ROTATION (ANGLE(4))
#define MAX_NUM_NODES   2000
#define MAX_NUM_EXTRA_VERTICES   2000
#define WORLD_MIN_X  (FIXEDPOINT(-16000))
#define WORLD_MAX_X  (FIXEDPOINT(16000))
#define WORLD_MIN_Y  (FIXEDPOINT(-16000))
#define WORLD_MAX_Y  (FIXEDPOINT(16000))
#define WORLD_MIN_Z  (FIXEDPOINT(-16000))
#define WORLD_MAX_Z  (FIXEDPOINT(16000))
#define PROJECTION_RATIO (2.0/1.0)  // controls field of view; the
                    // bigger this is, the narrower the field of view
typedef long FIXEDPOINT;
typedef struct _VERTEX {
   FIXEDPOINT x, z, viewx, viewz;
} VERTEX, *PVERTEX;
typedef struct _POINT2 { FIXEDPOINT x, z; } POINT2, *PPOINT2;
typedef struct _POINT2INT { int  x; int y; } POINT2INT, *PPOINT2INT;
typedef long ANGLE;     // angles are stored in degrees
typedef struct _NODE {
   VERTEX *pstartvertex, *pendvertex;
   FIXEDPOINT  walltop, wallbottom, tstart, tend;
   FIXEDPOINT  clippedtstart, clippedtend;
   struct _NODE *fronttree, *backtree;
   int         color, isVisible;
   FIXEDPOINT  screenxstart, screenxend;
   FIXEDPOINT  screenytopstart, screenybottomstart;
   FIXEDPOINT  screenytopend, screenybottomend;
} NODE, *PNODE;
char * pDIB;            // pointer to DIB section we'll draw into
HBITMAP hDIBSection;    // handle of DIB section
HPALETTE hpalDIB;
int iteration = 0, WorldIsRunning = 1;
HWND hwndOutput;
int DIBWidth, DIBHeight, DIBPitch, numvertices, numnodes;
FIXEDPOINT fxHalfDIBWidth, fxHalfDIBHeight;
VERTEX *pvertexlist, *pextravertexlist;
NODE *pnodelist;
POINT2 currentlocation, currentdirection, currentorientation;
ANGLE currentangle;
FIXEDPOINT currentspeed, fxViewerY, currentYSpeed;
FIXEDPOINT FrontClipPlane = FIXEDPOINT(10);
FIXEDPOINT FixedMul(FIXEDPOINT x, FIXEDPOINT y);
FIXEDPOINT FixedDiv(FIXEDPOINT x, FIXEDPOINT y);
FIXEDPOINT FixedSin(ANGLE angle), FixedCos(ANGLE angle);
extern int FillConvexPolygon(POINT2INT * VertexPtr, int Color);

// Returns nonzero if a wall is facing the viewer, 0 else.
int WallFacingViewer(NODE * pwall)
{
   FIXEDPOINT viewxstart = pwall->pstartvertex->viewx;
   FIXEDPOINT viewzstart = pwall->pstartvertex->viewz;
   FIXEDPOINT viewxend = pwall->pendvertex->viewx;
   FIXEDPOINT viewzend = pwall->pendvertex->viewz;
   int Temp;

/*  // equivalent C code
   if (( ((pwall->pstartvertex->viewx >> 16) *
         ((pwall->pendvertex->viewz -
          pwall->pstartvertex->viewz) >> 16)) +
         ((pwall->pstartvertex->viewz >> 16) *
          ((pwall->pstartvertex->viewx -
            pwall->pendvertex->viewx) >> 16)) )
               < 0)
      return(1);
   else
      return(0);
*/
   _asm {
      mov   eax,viewzend
      sub   eax,viewzstart
      imul  viewxstart
      mov   ecx,edx
      mov   ebx,eax
      mov   eax,viewxstart
      sub   eax,viewxend
      imul  viewzstart
      add   eax,ebx
      adc   edx,ecx
      mov   eax,0
      jns   short WFVDone
      inc   eax
WFVDone:
      mov   Temp,eax
   }
   return(Temp);
}

// Update the viewpoint position as needed.
void UpdateViewPos()
{
   if (currentspeed != 0) {
      currentlocation.x += FixedMul(currentdirection.x,
                                    currentspeed);
      if (currentlocation.x <= WORLD_MIN_X)
         currentlocation.x = WORLD_MIN_X;
      if (currentlocation.x >= WORLD_MAX_X)
         currentlocation.x = WORLD_MAX_X - 1;
      currentlocation.z += FixedMul(currentdirection.z,
                                    currentspeed);
      if (currentlocation.z <= WORLD_MIN_Z)
         currentlocation.z = WORLD_MIN_Z;
      if (currentlocation.z >= WORLD_MAX_Z)
         currentlocation.z = WORLD_MAX_Z - 1;
   }
   if (currentYSpeed != 0) {
      fxViewerY += currentYSpeed;
      if (fxViewerY <= WORLD_MIN_Y)
         fxViewerY = WORLD_MIN_Y;
      if (fxViewerY >= WORLD_MAX_Y)
         fxViewerY = WORLD_MAX_Y - 1;
   }
}

// Transform all vertices into viewspace.
void TransformVertices()
{
   VERTEX *pvertex;
   FIXEDPOINT tempx, tempz;
   int vertex;

   pvertex = pvertexlist;
   for (vertex = 0; vertex < numvertices; vertex++) {
      // Translate the vertex according to the viewpoint
      tempx = pvertex->x - currentlocation.x;
      tempz = pvertex->z - currentlocation.z;
      // Rotate the vertex so viewpoint is looking down z axis
      pvertex->viewx = FixedMul(FixedMul(tempx,
                                         currentorientation.z) +
                   FixedMul(tempz, -currentorientation.x),
                   FIXEDPOINT(PROJECTION_RATIO));
      pvertex->viewz = FixedMul(tempx, currentorientation.x) +
                   FixedMul(tempz, currentorientation.z);
      pvertex++;
   }
}

// 3D clip all walls. If any part of each wall is still visible,
// transform to perspective viewspace.
void ClipWalls()
{
   NODE *pwall;
   int wall;
   FIXEDPOINT tempstartx, tempendx, tempstartz, tempendz;
   FIXEDPOINT tempstartwalltop, tempstartwallbottom;
   FIXEDPOINT tempendwalltop, tempendwallbottom;
   VERTEX *pstartvertex, *pendvertex;
   VERTEX *pextravertex = pextravertexlist;

   pwall = pnodelist;
   for (wall = 0; wall < numnodes; wall++) {
      // Assume the wall won't be visible
      pwall->isVisible = 0;
      // Generate the wall endpoints, accounting for t values and
      // clipping
      // Calculate the viewspace coordinates for this wall
      pstartvertex = pwall->pstartvertex;
      pendvertex = pwall->pendvertex;
      // Look for z clipping first
      // Calculate start and end z coordinates for this wall
      if (pwall->tstart == FIXEDPOINT(0))
         tempstartz = pstartvertex->viewz;
      else
         tempstartz = pstartvertex->viewz +
               FixedMul((pendvertex->viewz-pstartvertex->viewz),
               pwall->tstart);
      if (pwall->tend == FIXEDPOINT(1))
         tempendz = pendvertex->viewz;
      else
         tempendz = pstartvertex->viewz +
               FixedMul((pendvertex->viewz-pstartvertex->viewz),
               pwall->tend);
      // Clip to the front plane
      if (tempendz < FrontClipPlane) {
         if (tempstartz < FrontClipPlane) {
            // Fully front-clipped
            goto NextWall;
         } else {
            pwall->clippedtstart = pwall->tstart;
            // Clip the end point to the front clip plane
            pwall->clippedtend =
                  FixedDiv(pstartvertex->viewz - FrontClipPlane,
                        pstartvertex->viewz-pendvertex->viewz);
            tempendz = pstartvertex->viewz +
               FixedMul((pendvertex->viewz-pstartvertex->viewz),
               pwall->clippedtend);
         }
      } else {
         pwall->clippedtend = pwall->tend;
         if (tempstartz < FrontClipPlane) {
            // Clip the start point to the front clip plane
            pwall->clippedtstart =
                  FixedDiv(FrontClipPlane - pstartvertex->viewz,
                        pendvertex->viewz-pstartvertex->viewz);
            tempstartz = pstartvertex->viewz +
               FixedMul((pendvertex->viewz-pstartvertex->viewz),
               pwall->clippedtstart);
         } else {
            pwall->clippedtstart = pwall->tstart;
         }
      }
      // Calculate x coordinates
      if (pwall->clippedtstart == FIXEDPOINT(0))
         tempstartx = pstartvertex->viewx;
      else
         tempstartx = pstartvertex->viewx +
               FixedMul((pendvertex->viewx-pstartvertex->viewx),
               pwall->clippedtstart);
      if (pwall->clippedtend == FIXEDPOINT(1))
         tempendx = pendvertex->viewx;
      else
         tempendx = pstartvertex->viewx +
               FixedMul((pendvertex->viewx-pstartvertex->viewx),
               pwall->clippedtend);
      // Clip in x as needed
      if ((tempstartx > tempstartz) || (tempstartx < -tempstartz)) {
         // The start point is outside the view triangle in x;
         // perform a quick test for trivial rejection by seeing if
         // the end point is outside the view triangle on the same
         // side as the start point
         if (((tempstartx>tempstartz) && (tempendx>tempendz)) ||
            ((tempstartx<-tempstartz) && (tempendx<-tempendz)))
            // Fully clipped--trivially reject
            goto NextWall;
         // Clip the start point
         if (tempstartx > tempstartz) {
            // Clip the start point on the right side
            pwall->clippedtstart =
               FixedDiv(pstartvertex->viewx-pstartvertex->viewz,
                      pendvertex->viewz-pstartvertex->viewz -
                      pendvertex->viewx+pstartvertex->viewx);
            tempstartx = pstartvertex->viewx +
               FixedMul((pendvertex->viewx-pstartvertex->viewx),
                       pwall->clippedtstart);
            tempstartz = tempstartx;
         } else {
            // Clip the start point on the left side
            pwall->clippedtstart =
               FixedDiv(-pstartvertex->viewx-pstartvertex->viewz,
                      pendvertex->viewx+pendvertex->viewz -
                      pstartvertex->viewz-pstartvertex->viewx);
            tempstartx = pstartvertex->viewx +
               FixedMul((pendvertex->viewx-pstartvertex->viewx),
                       pwall->clippedtstart);
            tempstartz = -tempstartx;
         }
      }
      // See if the end point needs clipping
      if ((tempendx > tempendz) || (tempendx < -tempendz)) {
         // Clip the end point
         if (tempendx > tempendz) {
            // Clip the end point on the right side
            pwall->clippedtend =
               FixedDiv(pstartvertex->viewx-pstartvertex->viewz,
                      pendvertex->viewz-pstartvertex->viewz -
                      pendvertex->viewx+pstartvertex->viewx);
            tempendx = pstartvertex->viewx +
               FixedMul((pendvertex->viewx-pstartvertex->viewx),
                       pwall->clippedtend);
            tempendz = tempendx;
         } else {
            // Clip the end point on the left side
            pwall->clippedtend =
               FixedDiv(-pstartvertex->viewx-pstartvertex->viewz,
                      pendvertex->viewx+pendvertex->viewz -
                      pstartvertex->viewz-pstartvertex->viewx);
            tempendx = pstartvertex->viewx +
               FixedMul((pendvertex->viewx-pstartvertex->viewx),
                       pwall->clippedtend);
            tempendz = -tempendx;
         }
      }
      tempstartwalltop = FixedMul((pwall->walltop - fxViewerY),
            FIXEDPOINT(PROJECTION_RATIO));
      tempendwalltop = tempstartwalltop;
      tempstartwallbottom = FixedMul((pwall->wallbottom-fxViewerY),
            FIXEDPOINT(PROJECTION_RATIO));
      tempendwallbottom = tempstartwallbottom;
      // Partially clip in y (the rest is done later in 2D)
      // Check for trivial accept
      if ((tempstartwalltop > tempstartz) ||
         (tempstartwallbottom < -tempstartz) ||
         (tempendwalltop > tempendz) ||
         (tempendwallbottom < -tempendz)) {
         // Not trivially unclipped; check for fully clipped
         if ((tempstartwallbottom > tempstartz) &&
            (tempstartwalltop < -tempstartz) &&
            (tempendwallbottom > tempendz) &&
            (tempendwalltop < -tempendz)) {
            // Outside view triangle, trivially clipped
            goto NextWall;
         }
         // Partially clipped in Y; we'll do Y clipping at
         // drawing time
      }
      // The wall is visible; mark it as such and project it.
      // +1 on scaling because of bottom/right exclusive polygon
      // filling
      pwall->isVisible = 1;
      pwall->screenxstart =
         (FixedMulDiv(tempstartx, fxHalfDIBWidth+FIXEDPOINT(0.5),
            tempstartz) + fxHalfDIBWidth + FIXEDPOINT(0.5));
      pwall->screenytopstart =
            (FixedMulDiv(tempstartwalltop,
            fxHalfDIBHeight + FIXEDPOINT(0.5), tempstartz) +
            fxHalfDIBHeight + FIXEDPOINT(0.5));
      pwall->screenybottomstart =
            (FixedMulDiv(tempstartwallbottom,
            fxHalfDIBHeight + FIXEDPOINT(0.5), tempstartz) +
            fxHalfDIBHeight + FIXEDPOINT(0.5));
      pwall->screenxend =
            (FixedMulDiv(tempendx, fxHalfDIBWidth+FIXEDPOINT(0.5),
            tempendz) + fxHalfDIBWidth + FIXEDPOINT(0.5));
      pwall->screenytopend =
            (FixedMulDiv(tempendwalltop,
            fxHalfDIBHeight + FIXEDPOINT(0.5), tempendz) +
            fxHalfDIBHeight + FIXEDPOINT(0.5));
      pwall->screenybottomend =
            (FixedMulDiv(tempendwallbottom,
            fxHalfDIBHeight + FIXEDPOINT(0.5), tempendz) +
            fxHalfDIBHeight + FIXEDPOINT(0.5));
NextWall:
      pwall++;
   }
}

// Walk the tree back to front; backface cull whenever possible,
// and draw front-facing walls in back-to-front order.
void DrawWallsBackToFront()
{
   NODE *pFarChildren, *pNearChildren, *pwall;
   NODE *pendingnodes[MAX_NUM_NODES];
   NODE **pendingstackptr;
   POINT2INT apoint[4];

   pwall = pnodelist;
   pendingnodes[0] = (NODE *)NULL;
   pendingstackptr = pendingnodes + 1;
   for (;;) {
      for (;;) {
         // Descend as far as possible toward the back,
         // remembering the nodes we pass through on the way.
         // Figure whether this wall is facing frontward or
         // backward; do in viewspace because non-visible walls
         // aren't projected into screenspace, and we need to
         // traverse all walls in the BSP tree, visible or not,
         // in order to find all the visible walls
         if (WallFacingViewer(pwall)) {
            // We're on the forward side of this wall, do the back
            // children first
            pFarChildren = pwall->backtree;
         } else {
            // We're on the back side of this wall, do the front
            // children first
            pFarChildren = pwall->fronttree;
         }
         if (pFarChildren == NULL)
            break;
         *pendingstackptr = pwall;
         pendingstackptr++;
         pwall = pFarChildren;
      }
      for (;;) {
         // See if the wall is even visible
         if (pwall->isVisible) {
            // See if we can backface cull this wall
            if (pwall->screenxstart < pwall->screenxend) {
               // Draw the wall
               apoint[0].x = FIXTOINT(pwall->screenxstart);
               apoint[1].x = FIXTOINT(pwall->screenxstart);
               apoint[2].x = FIXTOINT(pwall->screenxend);
               apoint[3].x = FIXTOINT(pwall->screenxend);
               apoint[0].y = FIXTOINT(pwall->screenytopstart);
               apoint[1].y = FIXTOINT(pwall->screenybottomstart);
               apoint[2].y = FIXTOINT(pwall->screenybottomend);
               apoint[3].y = FIXTOINT(pwall->screenytopend);
               FillConvexPolygon(apoint, pwall->color);
            }
         }
         // If there's a near tree from this node, draw it;
         // otherwise, work back up to the last-pushed parent
         // node of the branch we just finished; we're done if
         // there are no pending parent nodes.
         // Figure whether this wall is facing frontward or
         // backward; do in viewspace because non-visible walls
         // aren't projected into screenspace, and we need to
            // traverse all walls in the BSP tree, visible or not,
            // in order to find all the visible walls
         if (WallFacingViewer(pwall)) {
            // We're on the forward side of this wall, do the
            // front children now
            pNearChildren = pwall->fronttree;
         } else {
            // We're on the back side of this wall, do the back
            // children now
            pNearChildren = pwall->backtree;
         }
         // Walk the near subtree of this wall
         if (pNearChildren != NULL)
            goto WalkNearTree;
         // Pop the last-pushed wall
         pendingstackptr--;
         pwall = *pendingstackptr;
         if (pwall == NULL)
            goto NodesDone;
      }
WalkNearTree:
      pwall = pNearChildren;
   }
NodesDone:
;
}

// Render the current state of the world to the screen.
void UpdateWorld()
{
   HPALETTE holdpal;
   HDC hdcScreen, hdcDIBSection;
   HBITMAP holdbitmap;

   // Draw the frame
   UpdateViewPos();
   memset(pDIB, 0, DIBPitch*DIBHeight);    // clear frame
   TransformVertices();
   ClipWalls();
   DrawWallsBackToFront();
   // We've drawn the frame; copy it to the screen
   hdcScreen = GetDC(hwndOutput);
   holdpal = SelectPalette(hdcScreen, hpalDIB, FALSE);
   RealizePalette(hdcScreen);
   hdcDIBSection = CreateCompatibleDC(hdcScreen);
   holdbitmap = SelectObject(hdcDIBSection, hDIBSection);
   BitBlt(hdcScreen, 0, 0, DIBWidth, DIBHeight, hdcDIBSection,
          0, 0, SRCCOPY);
   SelectPalette(hdcScreen, holdpal, FALSE);
   ReleaseDC(hwndOutput, hdcScreen);
   SelectObject(hdcDIBSection, holdbitmap);
   ReleaseDC(hwndOutput, hdcDIBSection);
   iteration++;
}
