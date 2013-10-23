#include "PetscAdLemTaras3D.hxx"
#include "AdLem3D.hxx"

#undef __FUNCT__
#define __FUNCT__ "PetscAdLemTaras3D"
PetscAdLemTaras3D::PetscAdLemTaras3D(AdLem3D *model, bool writeParaToFile):
    PetscAdLem3D(model, std::string("Taras Method")),
    mWriteParaToFile((PetscBool)writeParaToFile)
{
    PetscErrorCode ierr;
    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"dmda of size: (%d,%d,%d)\n",
                            model->getXnum()+1,model->getYnum()+1,model->getZnum()+1);

    //DMDA for only pressure field that is used in Schur Complement solve Sp=rhs.
    ierr = DMDACreate3d(PETSC_COMM_WORLD,DMDA_BOUNDARY_NONE, DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,
                        DMDA_STENCIL_BOX,model->getXnum()+1,model->getYnum()+1,model->getZnum()+1,
                        PETSC_DECIDE,PETSC_DECIDE,PETSC_DECIDE,1,1,0,0,0,&mDaP);CHKERRXX(ierr);

//    ierr = DMDASetUniformCoordinates(mDaP,0,model->getXnum(),0,model->getYnum(),0,model->getZnum());CHKERRXX(ierr);
        ierr = DMDASetUniformCoordinates(mDaP,0,model->getXnum()+2,0,model->getYnum()+2,0,model->getZnum()+2);CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDaP,0,"p");CHKERRXX(ierr);

    ierr = DMDACreate3d(PETSC_COMM_WORLD,DMDA_BOUNDARY_NONE, DMDA_BOUNDARY_NONE,DMDA_BOUNDARY_NONE,
                        DMDA_STENCIL_BOX,model->getXnum()+1,model->getYnum()+1,model->getZnum()+1,
                        PETSC_DECIDE,PETSC_DECIDE,PETSC_DECIDE,4,1,0,0,0,&mDa);CHKERRXX(ierr);

//    ierr = DMDASetUniformCoordinates(mDa,0,model->getXnum(),0,model->getYnum(),0,model->getZnum());CHKERRXX(ierr);
    //    ierr = DMDASetUniformCoordinates(mDa,0,model->getXnum()+1,0,model->getYnum()+1,0,model->getZnum()+1);CHKERRXX(ierr);
        ierr = DMDASetUniformCoordinates(mDa,0,model->getXnum()+2,0,model->getYnum()+2,0,model->getZnum()+2);CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,0,"vx");CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,1,"vy");CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,2,"vz");CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,3,"p");CHKERRXX(ierr);


    //Linear Solver context:
    ierr = KSPCreate(PETSC_COMM_WORLD,&mKsp);CHKERRXX(ierr);

    createPcForSc();
    setNullSpace();

    mParaVecsCreated = PETSC_FALSE;

}

#undef __FUNCT__
#define __FUNCT__ "~PetscAdLemTaras3D"
PetscAdLemTaras3D::~PetscAdLemTaras3D()
{
    PetscErrorCode ierr;

    ierr = VecDestroy(&mNullBasis);CHKERRXX(ierr);
    ierr = MatNullSpaceDestroy(&mNullSpace);CHKERRXX(ierr);
    ierr = VecDestroy(&mNullBasisP);CHKERRXX(ierr);
    ierr = MatNullSpaceDestroy(&mNullSpaceP);CHKERRXX(ierr);

    ierr = MatDestroy(&mPcForSc);CHKERRXX(ierr);

    if(mParaVecsCreated) {
        ierr = VecDestroy(&mAtrophy);CHKERRXX(ierr);
        ierr = VecDestroy(&mMu);CHKERRXX(ierr);
    }

    ierr = DMDestroy(&mDaP);CHKERRXX(ierr);
}

#undef __FUNCT__
#define __FUNCT__ "setNullSpace"
void PetscAdLemTaras3D::setNullSpace()
{
    // Compute the Null space Basis vector i.e. constant pressure, let's set (0 0 ... 0 1 ... 1)
    //where all the pressure dof except the ghost pressures are set to 1. Others are set to 0.
    PetscInt ierr;
    ierr = DMCreateGlobalVector(mDa,&mNullBasis);CHKERRXX(ierr);
    DMDALocalInfo info;
    ierr = DMDAGetLocalInfo(mDa,&info);

    PetscAdLemTaras3D::Field    ***nullVec;
    ierr = DMDAVecGetArray(mDa, mNullBasis, &nullVec);CHKERRXX(ierr);

    //FIXME: Must handle c++ exception using the error codd PETSC_ERR_SUP, cannot use SETERRQ.
    /*if (this->getProblemModel()->getBcType() != this->getProblemModel()->DIRICHLET) {
        SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_SUP,"only Dirichlet boundary condition implemented",0);
    }*/
    for (PetscInt k=info.zs; k<info.zs+info.zm; ++k) {
        for (PetscInt j=info.ys; j<info.ys+info.ym; ++j) {
            for (PetscInt i=info.xs; i<info.xs+info.xm; ++i) {
                nullVec[k][j][i].vx = 0;
                nullVec[k][j][i].vy = 0;
                nullVec[k][j][i].vz = 0;
                if (i==0 || j==0 || k==0) //Ghost values
                    /*|| (i==1 && (j==1 || j==info.my-1 || k==1 || k==info.mz-1)) //four edges in x-start face.
                        || (i==info.mx-1 && (j==1 || j==info.my-1 || k==1 || k==info.mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== info.mz-1)) //two edges in y-start face.
                        || (j==info.my-1 && (k==1 || k==info.mz-1)))*/ { //two edges in y-end face

                    nullVec[k][j][i].p = 0;
                } else
                    nullVec[k][j][i].p = 1;
            }
        }
    }

    ierr = DMDAVecRestoreArray(mDa, mNullBasis, &nullVec);CHKERRXX(ierr);
    ierr = VecAssemblyBegin(mNullBasis);CHKERRXX(ierr);
    ierr = VecAssemblyEnd(mNullBasis);CHKERRXX(ierr);
    ierr = VecNormalize(mNullBasis,NULL);CHKERRXX(ierr);
    //Null Space context:
    ierr = MatNullSpaceCreate(PETSC_COMM_WORLD,PETSC_FALSE,1,&mNullBasis,&mNullSpace);

    ierr = DMCreateGlobalVector(mDaP,&mNullBasisP);CHKERRXX(ierr);

    ierr = VecStrideGather(mNullBasis,3,mNullBasisP,INSERT_VALUES);CHKERRXX(ierr); //insert the fourth field (pos:3, i.e. pressure)
    ierr = VecNormalize(mNullBasisP,NULL);CHKERRXX(ierr);

    //Null Space context:
    ierr = MatNullSpaceCreate(PETSC_COMM_WORLD,PETSC_FALSE,1,&mNullBasisP,&mNullSpaceP);CHKERRXX(ierr);
}

#undef __FUNCT__
#define __FUNCT__ "solveModel"
PetscErrorCode PetscAdLemTaras3D::solveModel()
{
    PetscErrorCode ierr;
    PetscFunctionBeginUser;
    ierr = KSPSetDM(mKsp,mDa);CHKERRQ(ierr);              //mDa with dof = 4, vx,vy,vz and p.
    ierr = DMKSPSetComputeOperators(mDa,computeMatrixTaras3D,this);CHKERRQ(ierr);
    ierr = DMKSPSetComputeRHS(mDa,computeRHSTaras3D,this);CHKERRQ(ierr);
    ierr = KSPSetNullSpace(mKsp,mNullSpace);CHKERRQ(ierr);//nullSpace for the main system
    ierr = KSPSetFromOptions(mKsp);CHKERRQ(ierr);
    ierr = KSPSetUp(mKsp);CHKERRQ(ierr);                  //register the fieldsplits obtained from options.

    ierr = KSPGetOperators(mKsp,&mA,NULL,NULL);CHKERRQ(ierr);
    ierr = KSPGetPC(mKsp,&mPc);CHKERRQ(ierr);

    PetscBool isNull;
    ierr = MatNullSpaceTest(mNullSpace,mA,&isNull);CHKERRQ(ierr);
    if(!isNull)
        SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"not a valid system null space \n");

    //Work out the fiedsplits and schur complement preconditioner from the options.
    //Set a Null space for Schur complement if schur pc type is used and fieldsplit 0,1,2 used.
    //Set a near-nullspace if gamg used for A00 block.
    PetscBool optionFlag = PETSC_FALSE;
    char optionString[PETSC_MAX_PATH_LEN];
    ierr = PetscOptionsGetString(NULL,"-pc_fieldsplit_type",optionString,10,&optionFlag);CHKERRQ(ierr);
    if(optionFlag) {
        if(strcmp(optionString,"schur")==0){
            PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n using schur complement \n");
            ierr = PetscOptionsGetString(NULL,"-pc_fieldsplit_0_fields",optionString,10,&optionFlag);CHKERRQ(ierr);
            if(optionFlag) {
                if(strcmp(optionString,"0,1,2")==0) {
                    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n using user defined split \n");
                    KSP *subKsp;
                    PetscInt subKspPos = 0;
                    ierr = PCFieldSplitSchurPrecondition(mPc,PC_FIELDSPLIT_SCHUR_PRE_USER,mPcForSc);CHKERRQ(ierr);
                    ierr = PetscOptionsGetString(NULL,"-fieldsplit_0_pc_type",optionString,10,&optionFlag);
                    if(strcmp(optionString,"gamg")==0) {
                        PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n using gamg for A00 \n");
                        //and near null space for gamg computed according to the split provided;
                        //Set up nearNullspace for A00 block.
                        ierr = PCFieldSplitGetSubKSP(mPc,&subKspPos,&subKsp);CHKERRQ(ierr);
                        MatNullSpace rigidBodyModes;
                        Vec coords;
                        ierr = DMGetCoordinates(mDa,&coords);CHKERRQ(ierr);
                        ierr = MatNullSpaceCreateRigidBody(coords,&rigidBodyModes);CHKERRQ(ierr);
                        Mat matA00;
                        ierr = KSPGetOperators(subKsp[0],&matA00,NULL,NULL);CHKERRQ(ierr);
                        ierr = MatSetNearNullSpace(matA00,rigidBodyModes);CHKERRQ(ierr);
                        ierr = MatNullSpaceDestroy(&rigidBodyModes);CHKERRQ(ierr);
                    }

                    //Position 1 => Ksp corresponding to Schur complement S on pressure space
                    subKspPos = 1;
                    ierr = PCFieldSplitGetSubKSP(mPc,&subKspPos,&subKsp);CHKERRQ(ierr);
                    //Set up the null space of constant pressure.
                    ierr = KSPSetNullSpace(subKsp[1],mNullSpaceP);CHKERRQ(ierr);
                    Mat matSc;
                    ierr = KSPGetOperators(subKsp[1],&matSc,NULL,NULL);CHKERRQ(ierr);
                    ierr = MatNullSpaceTest(mNullSpaceP,matSc,&isNull);
                    if(!isNull)
                        SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"not a valid pressure null space \n");

                    ierr = PetscFree(subKsp);CHKERRQ(ierr);
                }
            }
        }
    }

    ierr = KSPSolve(mKsp,NULL,NULL);CHKERRQ(ierr);
    ierr = KSPGetSolution(mKsp,&mX);CHKERRQ(ierr);
    ierr = KSPGetRhs(mKsp,&mB);CHKERRQ(ierr);

    ierr = getSolutionArray();CHKERRQ(ierr); //to get the local solution vector in each processor.

    PetscFunctionReturn(0);

}

#undef __FUNCT__
#define __FUNCT__ "writeToMatFile"
PetscErrorCode PetscAdLemTaras3D::writeToMatFile(
        const std::string& fileName, bool writeA,
        const std::string& matFileName)
{
    PetscErrorCode ierr;
    PetscFunctionBeginUser;
    PetscViewer viewer1;
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,fileName.c_str(),FILE_MODE_WRITE,&viewer1);CHKERRQ(ierr);
    ierr = PetscViewerSetFormat(viewer1,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);

    ierr = PetscObjectSetName((PetscObject)mX,"x");CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)mB,"b");CHKERRQ(ierr);

    ierr = VecView(mX,viewer1);CHKERRQ(ierr);
    ierr = VecView(mB,viewer1);CHKERRQ(ierr);

    Vec res; //residual
    ierr = VecDuplicate(mX,&res);CHKERRQ(ierr);
    ierr = VecSet(res,0);CHKERRQ(ierr);
    ierr = VecAXPY(res,-1.0,mB);CHKERRQ(ierr);
    ierr = MatMultAdd(mA,mX,res,res);
    ierr = PetscObjectSetName((PetscObject)res,"residual");CHKERRQ(ierr);
    ierr = VecView(res,viewer1);CHKERRQ(ierr);
    ierr = VecDestroy(&res);CHKERRQ(ierr);

    /*ierr = PetscObjectSetName((PetscObject)mNullBasis,"nullBasis");CHKERRQ(ierr);
    ierr = VecView(mNullBasis,viewer1);CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)mNullBasisP,"nullBasisP");CHKERRQ(ierr);
    ierr = VecView(mNullBasisP,viewer1);CHKERRQ(ierr);*/


    if(mWriteParaToFile) {
        createParaVectors();
        ierr = PetscObjectSetName((PetscObject)mAtrophy,"atrophy");CHKERRQ(ierr);
        ierr = PetscObjectSetName((PetscObject)mMu,"mu");CHKERRQ(ierr);
        ierr = VecView(mAtrophy,viewer1);CHKERRQ(ierr);
        ierr = VecView(mMu,viewer1);CHKERRQ(ierr);
    }
    ierr = PetscViewerDestroy(&viewer1);CHKERRQ(ierr);

    if(writeA) {
        PetscViewer viewer2;
        ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,matFileName.c_str(),FILE_MODE_WRITE,&viewer2);CHKERRQ(ierr);
        ierr = PetscViewerSetFormat(viewer2,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
        ierr = PetscObjectSetName((PetscObject)mA,"A");CHKERRQ(ierr);
        ierr = MatView(mA,viewer2);CHKERRQ(ierr);
        //        ierr = PetscObjectSetName((PetscObject)mPcForSc,"PcForSc");CHKERRQ(ierr);
        ierr = MatView(mPcForSc,viewer2);CHKERRQ(ierr);
        ierr = PetscViewerDestroy(&viewer2);CHKERRQ(ierr);
    }
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "createParaVectors"
PetscErrorCode PetscAdLemTaras3D::createParaVectors()
{
    mParaVecsCreated = PETSC_TRUE;
    PetscFunctionBeginUser;
    PetscInt ierr;

    ierr = DMCreateGlobalVector(mDaP,&mAtrophy);CHKERRQ(ierr);
    //    ierr = DMCreateGlobalVector(mDaP,&mMu);CHKERRQ(ierr);
    ierr = VecDuplicate(mAtrophy,&mMu);CHKERRQ(ierr);
    DMDALocalInfo info;
    ierr = DMDAGetLocalInfo(mDaP,&info);

    PetscReal    ***atrophyArray, ***muArray;
    ierr = DMDAVecGetArray(mDaP, mAtrophy, &atrophyArray);CHKERRQ(ierr);
    ierr = DMDAVecGetArray(mDaP, mMu, &muArray);CHKERRQ(ierr);

    for (PetscInt k=info.zs; k<info.zs+info.zm; ++k) {
        for (PetscInt j=info.ys; j<info.ys+info.ym; ++j) {
            for (PetscInt i=info.xs; i<info.xs+info.xm; ++i) {
                atrophyArray[k][j][i] = aC(i,j,k);
                muArray[k][j][i] = muC(i,j,k);
            }
        }
    }

    ierr = DMDAVecRestoreArray(mDaP, mAtrophy, &atrophyArray);CHKERRQ(ierr);
    ierr = VecAssemblyBegin(mAtrophy);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(mAtrophy);CHKERRQ(ierr);

    ierr = DMDAVecRestoreArray(mDaP, mMu, &muArray);CHKERRQ(ierr);
    ierr = VecAssemblyBegin(mMu);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(mMu);CHKERRQ(ierr);
    PetscFunctionReturn(0);
}

MatNullSpace PetscAdLemTaras3D::getNullSpace()
{
    return mNullSpace;
}

#undef __FUNCT__
#define __FUNCT__ "dataCenterAt"

/*AdLem3D class has data only at the cell centers.
The dimension of ghosted cell-centered data is greater by one in each
direction in Taras method. Hence, change the co-ordinate to get proper value
of the data at the cell center.*/
/*This is equivalent to increase the dimension of the image by one by copying the
  value of the faces sharing the origin to it's new neigbhouring face*/
PetscReal PetscAdLemTaras3D::dataCenterAt(std::string dType, PetscInt x, PetscInt y, PetscInt z)
{
    if(x != 0)
        --x;
    if(y != 0)
        --y;
    if(z != 0)
        --z;
    return this->getProblemModel()->dataAt(dType,x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "dataXyAt"
PetscReal PetscAdLemTaras3D::dataXyAt(std::string dType, PetscInt x, PetscInt y, PetscInt z)
{
    //For referencing, easier to say x-start, x-end face, edge etc!
    //on z-end faces: same as the values at z-1!
    if (z == this->getProblemModel()->getZnum())
        --z;

    //x-end and y-end corner point, simply return the corner value.
    if ((x == this->getProblemModel()->getXnum())
            && (y == this->getProblemModel()->getYnum())) {
        return dataCenterAt(dType,x,y,z+1);
    }

    //on x-end faces:
    if (x == this->getProblemModel()->getXnum())
        return 0.5*(dataCenterAt(dType,x,y,z+1) + dataCenterAt(dType,x,y+1,z+1));

    //on y-end faces:
    if (y == this->getProblemModel()->getYnum())
        return 0.5*(dataCenterAt(dType,x,y,z+1) + dataCenterAt(dType,x+1,y,z+1));

    return 0.25 * (dataCenterAt(dType,x,y,z+1) + dataCenterAt(dType,x,y+1,z+1)
                   + dataCenterAt(dType,x+1,y,z+1) + dataCenterAt(dType,x+1,y+1,z+1));
}

#undef __FUNCT__
#define __FUNCT__ "dataXz"
PetscReal PetscAdLemTaras3D::dataXzAt(std::string dType, PetscInt x, PetscInt y, PetscInt z)
{
    //on y-end faces: same as the values at y-1!
    if (y == this->getProblemModel()->getYnum())
        --y;

    //x-end and z-end corner point, simply return the corner value.
    if ((x == this->getProblemModel()->getXnum())
            && (z == this->getProblemModel()->getZnum())) {
        return dataCenterAt(dType,x,y+1,z);
    }

    //on x-end faces:
    if (x == this->getProblemModel()->getXnum())
        return 0.5*(dataCenterAt(dType,x,y+1,z) + dataCenterAt(dType,x,y+1,z+1));

    //on z-end faces:
    if (z == this->getProblemModel()->getZnum())
        return 0.5*(dataCenterAt(dType,x,y+1,z) + dataCenterAt(dType,x+1,y+1,z));

    return 0.25 * (dataCenterAt(dType,x,y+1,z) + dataCenterAt(dType,x,y+1,z+1)
                   + dataCenterAt(dType,x+1,y+1,z) + dataCenterAt(dType,x+1,y+1,z+1));
}

#undef __FUNCT__
#define __FUNCT__ "dataYz"
PetscReal PetscAdLemTaras3D::dataYzAt(std::string dType, PetscInt x, PetscInt y, PetscInt z)
{
    //on x-end faces: same as the values at x-1!
    if (x == this->getProblemModel()->getXnum())
        --x;

    //y-end and z-end corner point, simply return the corner value.
    if ((y == this->getProblemModel()->getXnum())
            && (z == this->getProblemModel()->getZnum())) {
        return dataCenterAt(dType,x+1,y,z);
    }

    //on y-end faces:
    if (y == this->getProblemModel()->getXnum())
        return 0.5*(dataCenterAt(dType,x+1,y,z) + dataCenterAt(dType,x+1,y,z+1));

    //on z-end faces:
    if (z == this->getProblemModel()->getZnum())
        return 0.5*(dataCenterAt(dType,x+1,y,z) + dataCenterAt(dType,x+1,y+1,z));

    return 0.25 * (dataCenterAt(dType,x+1,y,z) + dataCenterAt(dType,x+1,y,z+1)
                   + dataCenterAt(dType,x+1,y+1,z) + dataCenterAt(dType,x+1,y+1,z+1));
}

#undef __FUNCT__
#define __FUNCT__ "muC"
PetscReal PetscAdLemTaras3D::muC(PetscInt x, PetscInt y, PetscInt z)
{
    return dataCenterAt("mu",x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "lambdaC"
PetscReal PetscAdLemTaras3D::lambdaC(PetscInt x, PetscInt y, PetscInt z)
{
    return dataCenterAt("lambda",x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "aC"
PetscReal PetscAdLemTaras3D::aC(PetscInt x, PetscInt y, PetscInt z)
{
    return dataCenterAt("atrophy",x,y,z);
}

#undef __FUNCT__
#define __FUNCT_ "muXy"
PetscReal PetscAdLemTaras3D::muXy(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXyAt("mu",x,y,z);
}

#undef __FUNCT_
#define __FUNCT__ "muXz"
PetscReal PetscAdLemTaras3D::muXz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXzAt("mu",x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "muYz"
PetscReal PetscAdLemTaras3D::muYz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataYzAt("mu",x,y,z);
}

#undef __FUNCT__
#define __FUNCT_ "lambdaXy"
PetscReal PetscAdLemTaras3D::lambdaXy(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXyAt("lambda",x,y,z);
}

#undef __FUNCT_
#define __FUNCT__ "lambdaXz"
PetscReal PetscAdLemTaras3D::lambdaXz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXzAt("lambda",x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "lambdaYz"
PetscReal PetscAdLemTaras3D::lambdaYz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataYzAt("lambda",x,y,z);
}


#undef __FUNCT__
#define __FUNCT__ "createPcForSc"
void PetscAdLemTaras3D::createPcForSc()
{
    PetscInt        i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscErrorCode ierr;
    PetscFunctionBeginUser;

    ierr = DMDAGetInfo(mDaP,0,&mx,&my,&mz,0,0,0,0,0,0,0,0,0);CHKERRXX(ierr);
    ierr = DMDAGetCorners(mDaP,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRXX(ierr);

    ierr = DMSetMatrixPreallocateOnly(mDaP,PETSC_TRUE);CHKERRXX(ierr);
    ierr = DMCreateMatrix(mDaP,MATMPIAIJ,&mPcForSc);CHKERRXX(ierr);


    MatStencil row, col;
    PetscReal v;
    row.c = 0;
    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                /*if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1)) //two edges in y-end face
                        ) {
                    continue;
                } else {
                    ++mNumOfZeroDiag;
                }*/
                row.i = i;  row.j = j;  row.k = k;
                col.i = i;  col.j = j;  col.k = k;
                v = 1.0/muC(i,j,k);
                MatSetValuesStencil(mPcForSc,1,&row,1,&col,&v,INSERT_VALUES);CHKERRXX(ierr);
            }
        }
    }
    ierr = MatAssemblyBegin(mPcForSc,MAT_FINAL_ASSEMBLY);CHKERRXX(ierr);
    ierr = MatAssemblyEnd(mPcForSc,MAT_FINAL_ASSEMBLY);CHKERRXX(ierr);
    PetscFunctionReturnVoid();
}

/*
#undef __FUNCT__
#define __FUNCT__ "computeMatrixTaras3D"
PetscErrorCode PetscAdLemTaras3D::computeMatrixTaras3D(
        KSP ksp, Mat J, Mat jac, MatStructure *str, void *ctx)
{
    PetscAdLemTaras3D *user = (PetscAdLemTaras3D*)ctx;

    PetscErrorCode  ierr;
    PetscInt        i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscReal       Hx,Hy,Hz,HyHzdHx,HxHzdHy,HxHydHz;
    PetscScalar     v[17];
    MatStencil      row, col[17];
    DM              da;
    PetscReal       kBond = 1.0; //need to change it to scale the coefficients.
    PetscReal       kCont = 1.0; //need to change it to scale the coefficients.

    PetscFunctionBeginUser;
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da,0,&mx,&my,&mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx = 1;//1./(mx-1);
    Hy = 1;//1./(my-1);
    Hz = 1;//1./(mz-1);
    HyHzdHx = (Hy*Hz)/Hx;
    HxHzdHy = (Hx*Hz)/Hy;
    HxHydHz = (Hx*Hy)/Hz;
    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);

    if (user->getProblemModel()->getBcType() != user->getProblemModel()->DIRICHLET)
        SETERRQ1(PETSC_COMM_WORLD,PETSC_ERR_SUP,"only Dirichlet boundary condition supported",0);

    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                row.i = i; row.j = j; row.k = k;
                // ********************* x-momentum equation *******************
                row.c = 0;
                //Ghost vx unknowns(j=my-1,k=mz-1);boundary vx:(i=0,i=mx-1,j=0,j=my-2,k=0,k=mz-2)
                if (i==0 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vx. So let's
                    //initiate the component for these:
                    col[0].c = 0;       col[1].c = 0;

                    //x-start and x-end faces: vx(i,j,k) = 0
                    if (i==0 || i==mx-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //y-start, y-end; z-start, z-end faces:
                        //ghost values for j=my-1, k=mz-1: vx(i,j,k) = 0
                        if (j==my-1 || k==mz-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (j==0 || j==my-2) {//y-start and y-end faces
                                //3*vx(i,0,k) - vx(i,1,k) = 0; 3*vx(i,my-2,k) - vx(i,my-3,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (j==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                                }
                                else if (j==my-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //z-start and z-end faces
                                //3*vx(i,j,0) - vx(i,j,1) = 0; 3*vx(i,j,mz-2) - vx(i,j,mz-3) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (k==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                                }
                                else if (k==mz-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, x-momentum equation
                    //vx-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 0;

                    v[0] = 2.0*user->muC(i+1,j+1,k+1)*HyHzdHx;      col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = 2.0*user->muC(i,j+1,k+1)*HyHzdHx;        col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = user->muXy(i,j+1,k)*HxHzdHy;             col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = user->muXy(i,j,k)*HxHzdHy;               col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = user->muXz(i,j,k+1)*HxHydHz;             col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = user->muXz(i,j,k)*HxHydHz;               col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -v[0]-v[1]-v[2]-v[3]-v[4]-v[5];          col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //vy-coefficients, four terms.
                    for(int ii=7; ii<11; ++ii)
                        col[ii].c = 1;

                    v[7] = user->muXy(i,j+1,k)*Hz;                  col[7].i=i;     col[7].j=j+1;   col[7].k=k;
                    v[8] = -v[7];                                   col[8].i=i-1;   col[8].j=j+1;   col[8].k=k;
                    v[9] = user->muXy(i,j,k)*Hz;                    col[9].i=i-1;   col[9].j=j;     col[9].k=k;
                    v[10] = -v[9];                                  col[10].i=i;    col[10].j=j;    col[10].k=k;

                    //vz-coefficients, four terms.
                    for(int ii=11; ii<15; ++ii)
                        col[ii].c = 2;

                    v[11] = user->muXz(i,j,k+1)*Hy;                 col[11].i=i;    col[11].j=j;    col[11].k=k+1;
                    v[12] = -v[11];                                 col[12].i=i-1;  col[12].j=j;    col[12].k=k+1;
                    v[13] = user->muXz(i,j,k)*Hy;                   col[13].i=i-1;  col[13].j=j;    col[13].k=k;
                    v[14] = -v[13];                                 col[14].i=i;    col[14].j=j;    col[14].k=k;

                    //p-coefficients, two terms.
                    col[15].c = 3;      col[16].c = 3;
                    v[15] = kCont*Hy*Hz;        col[15].i=i;    col[15].j=j+1;  col[15].k=k+1;
                    v[16] = -v[15];             col[16].i=i+1;  col[16].j=j+1;  col[16].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,17,col,v,INSERT_VALUES);
                }
                // -*********************** y-momentum equation *******************
                row.c = 1;
                //Ghost vy unknowns(x=mx-1,k=mz-1);boundary vy:(i=0,i=mx-2,j=0,j=my-1,k=0,k=mz-2)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vy. So let's
                    //initiate the component for these:
                    col[0].c = 1;       col[1].c = 1;

                    //y-start and y-end faces: vy(i,j,k) = 0
                    if (j==0 || j==my-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //x-start, x-end; z-start, z-end faces:
                        //ghost values for i=mx-1, k=mz-1: vy(i,j,k) = 0
                        if (i==mx-1 || k==mz-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (i==0 || i==mx-2) {//x-start and x-end faces
                                //3*vy(0,j,k) - vy(1,j,k) = 0; 3*vy(mx-2,j,k) - vy(mx-3,j,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (i==0) {
                                    v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                                }
                                else if (i==mx-2) {
                                    v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //z-start and z-end faces
                                //3*vy(i,j,0) - vy(i,j,1) = 0; 3*vy(i,j,mz-2) - vy(i,j,mz-3) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (k==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                                }
                                else if (k==mz-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, y-momentum equation
                    //vy-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 1;

                    v[0] = 2.0*user->muC(i+1,j+1,k+1)*HxHzdHy;      col[0].i=i;     col[0].j=j+1;   col[0].k=k;
                    v[1] = 2.0*user->muC(i+1,j,k+1)*HxHzdHy;        col[1].i=i;     col[1].j=j-1;   col[1].k=k;
                    v[2] = user->muXy(i+1,j,k)*HyHzdHx;             col[2].i=i+1;   col[2].j=j;     col[2].k=k;
                    v[3] = user->muXy(i,j,k)*HyHzdHx;               col[3].i=i-1;   col[3].j=j;     col[3].k=k;
                    v[4] = user->muYz(i,j,k+1)*HxHydHz;             col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = user->muYz(i,j,k)*HxHydHz;               col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -v[0]-v[1]-v[2]-v[3]-v[4]-v[5];          col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //vx-coefficients, four terms.
                    for(int ii=7; ii<11; ++ii)
                        col[ii].c = 0;

                    v[7] = user->muXy(i+1,j,k)*Hz;                  col[7].i=i+1;   col[7].j=j;     col[7].k=k;
                    v[8] = -v[7];                                   col[8].i=i+1;   col[8].j=j-1;   col[8].k=k;
                    v[9] = user->muXy(i,j,k)*Hz;                    col[9].i=i;     col[9].j=j-1;   col[9].k=k;
                    v[10] = -v[9];                                  col[10].i=i;    col[10].j=j;    col[10].k=k;

                    //vz-coefficients, four terms.
                    for(int ii=11; ii<15; ++ii)
                        col[ii].c = 2;

                    v[11] = user->muYz(i,j,k+1)*Hx;                 col[11].i=i;    col[11].j=j;    col[11].k=k+1;
                    v[12] = -v[11];                                 col[12].i=i;    col[12].j=j-1;  col[12].k=k+1;
                    v[13] = user->muYz(i,j,k)*Hx;                   col[13].i=i;    col[13].j=j-1;  col[13].k=k;
                    v[14] = -v[13];                                 col[14].i=i;    col[14].j=j;    col[14].k=k;

                    //p-coefficients, two terms.
                    col[15].c = 3;      col[16].c = 3;
                    v[15] = kCont*Hx*Hz;        col[15].i=i+1;  col[15].j=j;    col[15].k=k+1;
                    v[16] = -v[15];             col[16].i=i+1;  col[16].j=j+1;  col[16].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,17,col,v,INSERT_VALUES);
                }

                // -*********************** z-momentum equation *******************
                row.c = 2;
                //Ghost vz unknowns(x=mx-1,y=my-1);boundary vz:(i=0,i=mx-2,j=0,j=my-2,k=0,k=mz-1)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vz. So let's
                    //initiate the component for these:
                    col[0].c = 2;       col[1].c = 2;

                    //z-start and z-end faces: vz(i,j,k) = 0
                    if (k==0 || k==mz-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //x-start, x-end; y-start, y-end faces:
                        //ghost values for i=mx-1, j=my-1: vz(i,j,k) = 0
                        if (i==mx-1 || j==my-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (i==0 || i==mx-2) {//x-start and x-end faces
                                //3*vz(0,j,k) - vz(1,j,k) = 0; 3*vz(mx-2,j,k) - vz(mx-3,j,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (i==0) {
                                    v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                                }
                                else if (i==mx-2) {
                                    v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //y-start and y-end faces
                                //3*vz(i,0,k) - vz(i,1,k) = 0; 3*vz(i,my-2,k) - vz(i,my-3,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (j==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                                }
                                else if (j==my-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, z-momentum equation
                    //vz-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 2;

                    v[0] = 2.0*user->muC(i+1,j+1,k+1)*HxHydHz;      col[0].i=i;     col[0].j=j;     col[0].k=k+1;
                    v[1] = 2.0*user->muC(i+1,j+1,k)*HxHydHz;        col[1].i=i;     col[1].j=j;     col[1].k=k-1;
                    v[2] = user->muXz(i+1,j,k)*HyHzdHx;             col[2].i=i+1;   col[2].j=j;     col[2].k=k;
                    v[3] = user->muXz(i,j,k)*HyHzdHx;               col[3].i=i-1;   col[3].j=j;     col[3].k=k;
                    v[4] = user->muYz(i,j+1,k)*HxHzdHy;             col[4].i=i;     col[4].j=j+1;   col[4].k=k;
                    v[5] = user->muYz(i,j,k)*HxHzdHy;               col[5].i=i;     col[5].j=j-1;   col[5].k=k;
                    v[6] = -v[0]-v[1]-v[2]-v[3]-v[4]-v[5];          col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //vx-coefficients, four terms.
                    for(int ii=7; ii<11; ++ii)
                        col[ii].c = 0;

                    v[7] = user->muXz(i+1,j,k)*Hy;                  col[7].i=i+1;   col[7].j=j;     col[7].k=k;
                    v[8] = -v[7];                                   col[8].i=i+1;   col[8].j=j;     col[8].k=k-1;
                    v[9] = user->muXz(i,j,k)*Hy;                    col[9].i=i;     col[9].j=j;     col[9].k=k-1;
                    v[10] = -v[9];                                  col[10].i=i;    col[10].j=j;    col[10].k=k;

                    //vy-coefficients, four terms.
                    for(int ii=11; ii<15; ++ii)
                        col[ii].c = 1;

                    v[11] = user->muYz(i,j+1,k)*Hx;                 col[11].i=i;    col[11].j=j+1;  col[11].k=k;
                    v[12] = -v[11];                                 col[12].i=i;    col[12].j=j+1;  col[12].k=k-1;
                    v[13] = user->muYz(i,j,k)*Hx;                   col[13].i=i;    col[13].j=j;    col[13].k=k-1;
                    v[14] = -v[13];                                 col[14].i=i;    col[14].j=j;    col[14].k=k;

                    //p-coefficients, two terms.
                    col[15].c = 3;      col[16].c = 3;
                    v[15] = kCont*Hx*Hy;        col[15].i=i+1;  col[15].j=j+1;  col[15].k=k;
                    v[16] = -v[15];             col[16].i=i+1;  col[16].j=j+1;  col[16].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,17,col,v,INSERT_VALUES);CHKERRQ(ierr);

                }

                // -********************** continuity equation *********************
                row.c = 3;
                if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1)) //two edges in y-end face
                        //|| (i==2 && j==2 && k==1) //constant pressure point NOT USED. INSTEAD TELL PETSC ABOUT THIS CONSTANT NULL-SPACE PRESSURE
                        ) {//BY DOING PCFIELDSPLIT. FIXME: MIGHT BE BETTER TO USE PCFIELDSPLIT EXPLICITLY HERE IN THE SOLUTION THAN LETTING IT AS
                    //COMMAND LINE OPTION SINCE WE MUST USE PCFIELDSPLIT IN THIS CASE!

                    //For all the ghost and boundary conditions we need at most two terms for p.
                    col[0].c = 3;       col[1].c = 3;
                    if (i==0 || j==0 || k==0) { //Ghost pressure p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i; col[0].j=j; col[0].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) {//four edges in x-start face.
                        //set dp/dx=0 i.e. p(i+1,j,k) - p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) { //four edges in x-end face.
                        //set dp/dx=0 i.e. p(i,j,k) - p(i-1,j,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (j==1 && (k==1 || k== mz-1)) { //two edges in y-start face.
                        //set dp/dy=0 i.e. p(i,j+1,k) - p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j+1;   col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (j==my-1 && (k==1 || k==mz-1)) { //two edges in y-end face
                        //set dp/dy=0 i.e. p(i,j,k) - p(i,j-1,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j-1;   col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } //else { //one cell NOTE: RHS needs to be set to kBond*pCell;
                      //  v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                      //  ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    //}
                } else {
                    //vx-coefficients, two terms
                    col[0].c = 0;       col[1].c = 0;
                    v[0] = kCont/Hx;    col[0].i = i;   col[0].j=j-1;   col[0].k=k-1;
                    v[1] = -v[0];       col[1].i = i-1; col[1].j=j-1;   col[1].k=k-1;

                    //vy-coefficients, two terms
                    col[2].c = 1;       col[3].c = 1;
                    v[2] = kCont/Hy;    col[2].i = i-1; col[2].j=j;     col[2].k=k-1;
                    v[3] = -v[2];       col[3].i = i-1; col[3].j=j-1;   col[3].k=k-1;

                    //vz-coefficients, two terms
                    col[4].c = 2;       col[5].c = 2;
                    v[4] = kCont/Hz;    col[4].i = i-1; col[4].j=j-1;   col[4].k=k;
                    v[5] = -v[4];       col[5].i = i-1; col[5].j=j-1;   col[5].k=k-1;

                    ierr=MatSetValuesStencil(jac,1,&row,6,col,v,INSERT_VALUES);CHKERRQ(ierr);
                }
            }
        }
    }
    ierr = MatAssemblyBegin(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}
*/

/*
#undef __FUNCT__
#define __FUNCT__ "computeRHSTaras3D"
PetscErrorCode PetscAdLemTaras3D::computeRHSTaras3D(KSP ksp, Vec b, void *ctx)
{
    PetscAdLemTaras3D    *user = (PetscAdLemTaras3D*)ctx;
    PetscErrorCode ierr;
    PetscInt       i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscScalar    Hx,Hy,Hz;
    PetscAdLemTaras3D::Field    ***rhs;
    DM             da;
    PetscReal       kCont=1.0;

    PetscFunctionBeginUser;
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da, 0, &mx, &my, &mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx   = 1;//1.0 / (PetscReal)(mx-1);
    Hy   = 1;//1.0 / (PetscReal)(my-1);
    Hz   = 1;//1.0 / (PetscReal)(mz-1);

    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);
    ierr = DMDAVecGetArray(da, b, &rhs);CHKERRQ(ierr);

    if (user->getProblemModel()->getBcType() != user->getProblemModel()->DIRICHLET) {
        SETERRQ1(PETSC_COMM_WORLD,PETSC_ERR_SUP,"only Dirichlet boundary condition implemented",0);
    }
    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                //Ghost vx unknowns(j=my-1,k=mz-1);boundary vx:(i=0,i=mx-1,j=0,j=my-2,k=0,k=mz-2)
                if (i==0 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    rhs[k][j][i].vx = 0;
                } else { //interior points, x-momentum equation
                    rhs[k][j][i].vx = Hy*Hz*(user->muC(i+1,j+1,k+1) + user->muC(i,j+1,k+1)
                                             +user->lambdaC(i+1,j+1,k+1) + user->lambdaC(i,j+1,k+1)
                                             )*(user->aC(i+1,j+1,k+1) - user->aC(i,j+1,k+1))/2.0;
                }
                // *********************** y-momentum equation *******************
                //Ghost vy unknowns(x=mx-1,k=mz-1);boundary vy:(i=0,i=mx-2,j=0,j=my-1,k=0,k=mz-2)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    rhs[k][j][i].vy = 0;
                } else { //interior points, y-momentum equation
                    rhs[k][j][i].vy = Hx*Hz*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j,k+1)
                                             +user->lambdaC(i+1,j+1,k+1) + user->lambdaC(i+1,j,k+1)
                                             )*(user->aC(i+1,j+1,k+1) - user->aC(i+1,j,k+1))/2.0;
                }

                // *********************** z-momentum equation *******************
                //Ghost vz unknowns(x=mx-1,y=my-1);boundary vz:(i=0,i=mx-2,j=0,j=my-2,k=0,k=mz-1)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-1) {
                    rhs[k][j][i].vz = 0;
                } else { //interior points, z-momentum equation
                    rhs[k][j][i].vz = Hx*Hy*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j+1,k)
                                             +user->lambdaC(i+1,j+1,k+1) + user->lambdaC(i+1,j+1,k)
                                             )*(user->aC(i+1,j+1,k+1) - user->aC(i+1,j+1,k))/2.0;
                }

                //  ********************** continuity equation *********************
                if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1))) { //two edges in y-end face
                    rhs[k][j][i].p = 0;
                } //else if (i==2 && j==1 && k==1) {//constant pressure point
                //  rhs[k][j][i].p = kCont*user->getP0Cell();
                //}
                else {
                    rhs[k][j][i].p = -kCont*user->aC(i,j,k);
                }
            }
        }
    }

    ierr = DMDAVecRestoreArray(da, b, &rhs);CHKERRQ(ierr);
    //    ierr = VecAssemblyBegin(b);CHKERRQ(ierr);
    //    ierr = VecAssemblyEnd(b);CHKERRQ(ierr);
    //    ierr = MatNullSpaceRemove(user->getNullSpace(),b,NULL);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}*/

#undef __FUNCT__
#define __FUNCT__ "computeMatrixTaras3D"
PetscErrorCode PetscAdLemTaras3D::computeMatrixTaras3D(
        KSP ksp, Mat J, Mat jac, MatStructure *str, void *ctx)
{
    PetscAdLemTaras3D *user = (PetscAdLemTaras3D*)ctx;

    PetscErrorCode  ierr;
    PetscInt        i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscReal       Hx,Hy,Hz,HyHzdHx,HxHzdHy,HxHydHz;
    PetscScalar     v[9];
    MatStencil      row, col[9];
    DM              da;
    PetscReal       kBond = 1.0; //need to change it to scale the coefficients.
    PetscReal       kCont = 1.0; //need to change it to scale the coefficients.

    PetscFunctionBeginUser;
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da,0,&mx,&my,&mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx = 1;//1./(mx-1);
    Hy = 1;//1./(my-1);
    Hz = 1;//1./(mz-1);
    HyHzdHx = (Hy*Hz)/Hx;
    HxHzdHy = (Hx*Hz)/Hy;
    HxHydHz = (Hx*Hy)/Hz;
    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);

    if (user->getProblemModel()->getBcType() != user->getProblemModel()->DIRICHLET)
        SETERRQ1(PETSC_COMM_WORLD,PETSC_ERR_SUP,"only Dirichlet boundary condition supported",0);

    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                row.i = i; row.j = j; row.k = k;
                // ********************* x-momentum equation *******************
                row.c = 0;
                //Ghost vx unknowns(j=my-1,k=mz-1);boundary vx:(i=0,i=mx-1,j=0,j=my-2,k=0,k=mz-2)
                if (i==0 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vx. So let's
                    //initiate the component for these:
                    col[0].c = 0;       col[1].c = 0;

                    //x-start and x-end faces: vx(i,j,k) = 0
                    if (i==0 || i==mx-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //y-start, y-end; z-start, z-end faces:
                        //ghost values for j=my-1, k=mz-1: vx(i,j,k) = 0
                        if (j==my-1 || k==mz-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (j==0 || j==my-2) {//y-start and y-end faces
                                //3*vx(i,0,k) - vx(i,1,k) = 0; 3*vx(i,my-2,k) - vx(i,my-3,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (j==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                                }
                                else if (j==my-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                                }
//                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                                //CHANGED:
                                v[0] = kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //z-start and z-end faces
                                //3*vx(i,j,0) - vx(i,j,1) = 0; 3*vx(i,j,mz-2) - vx(i,j,mz-3) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (k==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                                }
                                else if (k==mz-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                                }
//                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                                //CHANGED
                                v[0] = kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, x-momentum equation
                    //vx-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 0;

                    PetscScalar coeff = (user->muC(i+1,j+1,k+1) + user->muC(i,j+1,k+1))/2.;
                    v[0] = coeff*HyHzdHx;          col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = coeff*HyHzdHx;          col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = coeff*HxHzdHy;          col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = coeff*HxHzdHy;          col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = coeff*HxHydHz;          col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = coeff*HxHydHz;          col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -2*(v[0]+v[2]+v[4]);    col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //p-coefficients, two terms.
                    col[7].c = 3;      col[8].c = 3;
                    v[7] = kCont*Hy*Hz;        col[7].i=i;    col[7].j=j+1;  col[7].k=k+1;
                    v[8] = -v[7];              col[8].i=i+1;  col[8].j=j+1;  col[8].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,9,col,v,INSERT_VALUES);
                }
                //*********************** y-momentum equation *******************
                row.c = 1;
                //Ghost vy unknowns(x=mx-1,k=mz-1);boundary vy:(i=0,i=mx-2,j=0,j=my-1,k=0,k=mz-2)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vy. So let's
                    //initiate the component for these:
                    col[0].c = 1;       col[1].c = 1;

                    //y-start and y-end faces: vy(i,j,k) = 0
                    if (j==0 || j==my-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //x-start, x-end; z-start, z-end faces:
                        //ghost values for i=mx-1, k=mz-1: vy(i,j,k) = 0
                        if (i==mx-1 || k==mz-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (i==0 || i==mx-2) {//x-start and x-end faces
                                //3*vy(0,j,k) - vy(1,j,k) = 0; 3*vy(mx-2,j,k) - vy(mx-3,j,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (i==0) {
                                    v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                                }
                                else if (i==mx-2) {
                                    v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                                }
//                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                                //CHANGED:
                                v[0] = kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //z-start and z-end faces
                                //3*vy(i,j,0) - vy(i,j,1) = 0; 3*vy(i,j,mz-2) - vy(i,j,mz-3) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (k==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                                }
                                else if (k==mz-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                                }
//                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                                //CHANGED:
                                v[0] = kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, y-momentum equation
                    //vy-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 1;

                    PetscScalar coeff = (user->muC(i+1,j+1,k+1) + user->muC(i+1,j,k+1))/2.;
                    v[0] = coeff*HyHzdHx;          col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = coeff*HyHzdHx;          col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = coeff*HxHzdHy;          col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = coeff*HxHzdHy;          col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = coeff*HxHydHz;          col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = coeff*HxHydHz;          col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -2*(v[0]+v[2]+v[4]);    col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //p-coefficients, two terms.
                    col[7].c = 3;      col[8].c = 3;
                    v[7] = kCont*Hx*Hz;       col[7].i=i+1;  col[7].j=j;    col[7].k=k+1;
                    v[8] = -v[7];             col[8].i=i+1;  col[8].j=j+1;  col[8].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,9,col,v,INSERT_VALUES);
                }

                //*********************** z-momentum equation *******************
                row.c = 2;
                //Ghost vz unknowns(x=mx-1,y=my-1);boundary vz:(i=0,i=mx-2,j=0,j=my-2,k=0,k=mz-1)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vz. So let's
                    //initiate the component for these:
                    col[0].c = 2;       col[1].c = 2;

                    //z-start and z-end faces: vz(i,j,k) = 0
                    if (k==0 || k==mz-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //x-start, x-end; y-start, y-end faces:
                        //ghost values for i=mx-1, j=my-1: vz(i,j,k) = 0
                        if (i==mx-1 || j==my-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (i==0 || i==mx-2) {//x-start and x-end faces
                                //3*vz(0,j,k) - vz(1,j,k) = 0; 3*vz(mx-2,j,k) - vz(mx-3,j,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (i==0) {
                                    v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                                }
                                else if (i==mx-2) {
                                    v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                                }
//                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                                //CHANGED
                                v[0] = kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //y-start and y-end faces
                                //3*vz(i,0,k) - vz(i,1,k) = 0; 3*vz(i,my-2,k) - vz(i,my-3,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (j==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                                }
                                else if (j==my-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                                }
//                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                                //CHANGED
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, z-momentum equation
                    //vz-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 2;

                    PetscScalar coeff = (user->muC(i+1,j+1,k+1) + user->muC(i+1,j+1,k))/2.;
                    v[0] = coeff*HyHzdHx;          col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = coeff*HyHzdHx;          col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = coeff*HxHzdHy;          col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = coeff*HxHzdHy;          col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = coeff*HxHydHz;          col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = coeff*HxHydHz;          col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -2*(v[0]+v[2]+v[4]);    col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //p-coefficients, two terms.
                    col[7].c = 3;      col[8].c = 3;
                    v[7] = kCont*Hx*Hy;        col[7].i=i+1;  col[7].j=j+1;  col[7].k=k;
                    v[8] = -v[7];              col[8].i=i+1;  col[8].j=j+1;  col[8].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,9,col,v,INSERT_VALUES);CHKERRQ(ierr);

                }

                //********************** continuity equation *********************
                row.c = 3;
                if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1)) //two edges in y-end face
                        //|| (i==2 && j==2 && k==1) //constant pressure point NOT USED. INSTEAD TELL PETSC ABOUT THIS CONSTANT NULL-SPACE PRESSURE
                        ) {//BY DOING PCFIELDSPLIT. FIXME: MIGHT BE BETTER TO USE PCFIELDSPLIT EXPLICITLY HERE IN THE SOLUTION THAN LETTING IT AS
                    //COMMAND LINE OPTION SINCE WE MUST USE PCFIELDSPLIT IN THIS CASE!

                    //For all the ghost and boundary conditions we need at most two terms for p.
                    col[0].c = 3;       col[1].c = 3;
                    if (i==0 || j==0 || k==0) { //Ghost pressure p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i; col[0].j=j; col[0].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) {//four edges in x-start face.
                        //set dp/dx=0 i.e. p(i+1,j,k) - p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) { //four edges in x-end face.
                        //set dp/dx=0 i.e. p(i,j,k) - p(i-1,j,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (j==1 && (k==1 || k== mz-1)) { //two edges in y-start face.
                        //set dp/dy=0 i.e. p(i,j+1,k) - p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j+1;   col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (j==my-1 && (k==1 || k==mz-1)) { //two edges in y-end face
                        //set dp/dy=0 i.e. p(i,j,k) - p(i,j-1,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j-1;   col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } //else { //one cell NOTE: RHS needs to be set to kBond*pCell;
                        //v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                        //ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    //}
                } else {
                    //vx-coefficients, two terms
                    col[0].c = 0;       col[1].c = 0;
                    v[0] = kCont/Hx;    col[0].i = i;   col[0].j=j-1;   col[0].k=k-1;
                    v[1] = -v[0];       col[1].i = i-1; col[1].j=j-1;   col[1].k=k-1;

                    //vy-coefficients, two terms
                    col[2].c = 1;       col[3].c = 1;
                    v[2] = kCont/Hy;    col[2].i = i-1; col[2].j=j;     col[2].k=k-1;
                    v[3] = -v[2];       col[3].i = i-1; col[3].j=j-1;   col[3].k=k-1;

                    //vz-coefficients, two terms
                    col[4].c = 2;       col[5].c = 2;
                    v[4] = kCont/Hz;    col[4].i = i-1; col[4].j=j-1;   col[4].k=k;
                    v[5] = -v[4];       col[5].i = i-1; col[5].j=j-1;   col[5].k=k-1;

                    ierr=MatSetValuesStencil(jac,1,&row,6,col,v,INSERT_VALUES);CHKERRQ(ierr);
                }
            }
        }
    }
    ierr = MatAssemblyBegin(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "computeRHSTaras3D"
PetscErrorCode PetscAdLemTaras3D::computeRHSTaras3D(KSP ksp, Vec b, void *ctx)
{
    PetscAdLemTaras3D    *user = (PetscAdLemTaras3D*)ctx;
    PetscErrorCode ierr;
    PetscInt       i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscScalar    Hx,Hy,Hz;
    PetscAdLemTaras3D::Field    ***rhs;
    DM             da;
    PetscReal       kCont=1.0;

    PetscFunctionBeginUser;
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da, 0, &mx, &my, &mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx   = 1;//1.0 / (PetscReal)(mx-1);
    Hy   = 1;//1.0 / (PetscReal)(my-1);
    Hz   = 1;//1.0 / (PetscReal)(mz-1);

    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);
    ierr = DMDAVecGetArray(da, b, &rhs);CHKERRQ(ierr);

    if (user->getProblemModel()->getBcType() != user->getProblemModel()->DIRICHLET) {
        SETERRQ1(PETSC_COMM_WORLD,PETSC_ERR_SUP,"only Dirichlet boundary condition implemented",0);
    }
    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                //Ghost vx unknowns(j=my-1,k=mz-1);boundary vx:(i=0,i=mx-1,j=0,j=my-2,k=0,k=mz-2)
                if (i==0 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-1){
                    rhs[k][j][i].vx = 0;
                } else if ( k==mz-2){
                    rhs[k][j][i].vx = 30;
                } else { //interior points, x-momentum equation
                    rhs[k][j][i].vx = 0;
                }
                // *********************** y-momentum equation *******************
                //Ghost vy unknowns(x=mx-1,k=mz-1);boundary vy:(i=0,i=mx-2,j=0,j=my-1,k=0,k=mz-2)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    rhs[k][j][i].vy = 0;
                } else { //interior points, y-momentum equation
                    rhs[k][j][i].vy = 0;
                }

                // *********************** z-momentum equation *******************
                //Ghost vz unknowns(x=mx-1,y=my-1);boundary vz:(i=0,i=mx-2,j=0,j=my-2,k=0,k=mz-1)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-1) {
                    rhs[k][j][i].vz = 0;
                } else { //interior points, z-momentum equation
                    rhs[k][j][i].vz = 0;
                }

                //  ********************** continuity equation *********************
                if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1))) { //two edges in y-end face
                    rhs[k][j][i].p = 0;
                } //else if (i==2 && j==1 && k==1) {//constant pressure point
                //  rhs[k][j][i].p = kCont*user->getP0Cell();
                //}
                else {
                    rhs[k][j][i].p = 0;//-kCont*user->aC(i,j,k);
                }
            }
        }
    }

    ierr = DMDAVecRestoreArray(da, b, &rhs);CHKERRQ(ierr);
    //    ierr = VecAssemblyBegin(b);CHKERRQ(ierr);
    //    ierr = VecAssemblyEnd(b);CHKERRQ(ierr);
//        ierr = MatNullSpaceRemove(user->getNullSpace(),b,NULL);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}

