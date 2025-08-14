#include <vector>
#include <set>

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

INode* GetSkinRootBones(INode* skinnedNode, Interface* ip);

// 查找皮肤修改器
Modifier* FindSkinModifier(Object* obj, Interface* ip)
{
    if (!obj) return nullptr;

    IDerivedObject* derivedObj = nullptr;

    // 检查是否可直接获取修改器堆栈
    if (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        derivedObj = (IDerivedObject*)obj;
    }
    else if (obj->CanConvertToType(Class_ID(DERIVOB_CLASS_ID, 0))) {
        derivedObj = (IDerivedObject*)obj->ConvertToType(ip->GetTime(), Class_ID(DERIVOB_CLASS_ID, 0));
    }

    if (!derivedObj) return nullptr;

    // 遍历修改器堆栈
    for (int modIdx = 0; modIdx < derivedObj->NumModifiers(); modIdx++) {
        Modifier* mod = derivedObj->GetModifier(modIdx);
        if (mod->ClassID() == SKIN_CLASSID) {
            return mod;
        }
    }

    return nullptr;
}

// 辅助函数：查找 Skin 修改器
//Modifier* FindSkinModifier(Object* obj)
//{
//    if (!obj) return nullptr;
//
//    IDerivedObject* derivedObj = nullptr;
//
//    if (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
//        derivedObj = static_cast<IDerivedObject*>(obj);
//    }
//    else if (obj->CanConvertToType(Class_ID(DERIVOB_CLASS_ID, 0))) {
//        derivedObj = static_cast<IDerivedObject*>(obj->ConvertToType(GetCOREInterface()->GetTime(), Class_ID(DERIVOB_CLASS_ID, 0)));
//    }
//
//    if (!derivedObj) return nullptr;
//
//    for (int i = 0; i < derivedObj->NumModifiers(); i++) {
//        Modifier* mod = derivedObj->GetModifier(i);
//        if (mod->ClassID() == SKIN_CLASSID) {
//            return mod;
//        }
//
//        // 检查皮肤包裹器（Skin Wrap）
//        if (mod->ClassID() == SKINWRAPCLASSID) {
//            return mod;
//        }
//    }
//
//    return nullptr;
//}

// 重置节点变换，处理蒙皮
void ResetNodeTransform(INode* node, Interface* ip)
{
    if (!node || !ip) return;

    // 1. 处理蒙皮重置
    if (Object* obj = node->GetObjectRef()) {
        // 查找 Skin 修改器
        Modifier* skinMod = FindSkinModifier(obj, ip);
        if (skinMod) {
            ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
            if (skin) {
                // 重置蒙皮变换
                //skin->ResetTransforms(skinMod);
                skinMod->ReleaseInterface(I_SKIN, skin);
            }
        }
    }

    TimeValue t = ip->GetTime();

    auto rootBone = GetSkinRootBones(node, ip);
    if (rootBone)
    {
        Matrix3 mat = rootBone->GetNodeTM(t);
        Point3 translation = mat.GetTrans();
        AffineParts parts;
        float  ang[3];

        OutputDebugString(rootBone->GetName());
        decomp_affine(mat, &parts);
        QuatToEuler(parts.q, ang);

        mat.SetTrans(Point3::Origin); // 位移归零
        rootBone->SetNodeTM(t, mat);
    }
    
    Matrix3 mat = node->GetNodeTM(t);
    mat.SetTrans(Point3::Origin);
    node->SetNodeTM(t, mat);

    //// 2. 保存子节点列表（非引用计数）
    //Tab<INode*> childNodes;
    //for (int i = 0; i < node->NumberOfChildren(); i++) {
    //    childNodes.Append(1, &node->GetChildNode(i), 5);
    //}

    //// 3. 解除子节点
    //HoldSuspend suspend; // 暂停保持标记
    //for (int i = 0; i < childNodes.Count(); i++) {
    //    if (childNodes[i]) {
    //        childNodes[i]->Detach(0, 0); // 从父节点分离
    //    }
    //}

    // 4. 重置节点变换
//    {
//        SuspendAnimate(); // 暂停动画录制
//        TimeValue t = ip->GetTime();
//
//#if 0
//        // 位置归零
//        Control* posCtrl = node->GetTMController()->GetPositionController();
//        if (posCtrl && posCtrl->IsKeyable()) {
//            posCtrl->SetValue(t, (void*)&Point3::Origin);
//        }
//
//        // 缩放重置为1
//        Control* scaleCtrl = node->GetTMController()->GetScaleController();
//        if (scaleCtrl && scaleCtrl->IsKeyable()) {
//            ScaleValue scaleValue(Point3(1.0f, 1.0f, 1.0f));
//            scaleCtrl->SetValue(t, (void*)&scaleValue);
//        }
//
//        // 旋转重置
//        Control* rotCtrl = node->GetTMController()->GetRotationController();
//        if (rotCtrl && rotCtrl->IsKeyable()) {
//            Quat quat;
//            quat.Identity();
//            rotCtrl->SetValue(t, (void*)&quat);
//        }
//#else
//        Matrix3 mat = node->GetObjectTM(t);
//        Point3 translation = mat.GetTrans();
//        //Point3 scaling = mat.get
//        Point3 rotation;
//        mat.GetYawPitchRoll(&rotation.x, &rotation.y, &rotation.z);
//
//        mat = node->GetNodeTM(t);
//
//        node->SetNodeTM(t, Matrix3::Identity);
//
//        node->InvalidateTM();
//        node->InvalidateWS();
//#endif
//    }

    //// 5. 重新附加子节点
    //for (int i = 0; i < childNodes.Count(); i++) {
    //    if (childNodes[i]) {
    //        node->AttachChild(childNodes[i], 0, 0);
    //    }
    //}
}


// 获取蒙皮系统的根骨骼节点列表
INode* GetSkinRootBones(INode* skinnedNode, Interface* ip)
{
    std::vector<INode*> rootBones;
    if (!skinnedNode) return nullptr;

    // 1. 获取对象引用和派生对象
    Object* obj = skinnedNode->GetObjectRef();
    if (!obj) return nullptr;

    // 2. 查找 Skin 修改器
    Modifier* skinMod = FindSkinModifier(obj, ip);
    if (!skinMod) return nullptr;

    // 3. 获取 ISkin 接口
    ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
    if (!skin) return nullptr;

    // 4. 获取骨骼数量并准备数据结构
    int boneCount = skin->GetNumBones();
    std::set<INode*> boneNodes;

    // 5. 收集所有骨骼节点
    for (int i = 0; i < boneCount; i++) {
        INode* boneNode = skin->GetBone(i);
        if (boneNode) {
            boneNodes.insert(boneNode);
        }
    }

    // 6. 过滤根骨骼（没有父骨骼或父骨骼不在骨骼列表中）
    for (INode* bone : boneNodes) {
        INode* parent = bone->GetParentNode();

        // 检查父骨骼是否在骨骼列表中
        bool isRoot = true;
        if (parent) {
            isRoot = (boneNodes.find(parent) == boneNodes.end());
        }

        if (isRoot) {
            rootBones.push_back(bone);
        }
    }

    // 7. 释放接口
    skinMod->ReleaseInterface(I_SKIN, skin);

    if (rootBones.size() == 1)
    {
        INode* parent = rootBones.front()->GetParentNode();
        if (parent)
        {
            return parent;
        }
    }

    return nullptr;
}

