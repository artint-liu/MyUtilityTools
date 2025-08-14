#include <inode.h>
#include <object.h>
#include <modstack.h>
//#include <imod.h>
#include <ISkin.h>
#include <control.h>
#include <matrix3.h>
#include <point3.h>
#include <quat.h>
#include <decomp.h>
#include <triobj.h>
#include "meshdelta.h"

#include "utility.h"

//void RepairBoundary(INode* refnode, INode* node, Interface* ip)
//{
//    std::vector<UserPoint> userPoint;
//    Mesh* pMesh;
//    Matrix3 mat;
//
//    TriObject* triRefObj = GetMeshFromNode(refnode, ip->GetTime(), pMesh, mat);
//    //MarkBoundary(pMesh);
//    GenerateBoundary(pMesh, userPoint);
//
//    //{
//    //    MeshDelta md(*pMesh);
//    //    TestMoveMesh(pMesh);
//    //    md.Apply(*pMesh);
//    //    triRefObj->UpdateValidity(GEOM_CHAN_NUM, Interval(ip->GetTime(), ip->GetTime()));
//    //    triRefObj->NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
//    //    node->NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
//    //}
//
//    if (triRefObj)
//    {
//        triRefObj->DeleteThis();
//    }
//
//    TriObject* triNodeObj = GetMeshFromNode(node, ip->GetTime(), pMesh, mat);
//    //MarkBoundary(pMesh);
//    MeshDelta md(*pMesh);
//    SetupBoundary(pMesh, userPoint, 0.2f);
//
//    if (triNodeObj)
//    {
//        md.Apply(*pMesh);
//        triNodeObj->UpdateValidity(GEOM_CHAN_NUM, Interval(ip->GetTime(), ip->GetTime()));
//        triNodeObj->NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
//        node->NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
//        triNodeObj->DeleteThis();
//    }
//}
