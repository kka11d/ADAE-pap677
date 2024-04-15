# --------------------------------------------------------------------

cdef extern from* nogil:

    PetscErrorCode DMLabelCreate(MPI_Comm,char[],PetscDMLabel*)
    PetscErrorCode DMLabelView(PetscDMLabel,PetscViewer)
    PetscErrorCode DMLabelReset(PetscDMLabel)
    PetscErrorCode DMLabelDestroy(PetscDMLabel*)
    PetscErrorCode DMLabelGetDefaultValue(PetscDMLabel,PetscInt*)
    PetscErrorCode DMLabelSetDefaultValue(PetscDMLabel,PetscInt)
    PetscErrorCode DMLabelDuplicate(PetscDMLabel,PetscDMLabel*)
    PetscErrorCode DMLabelGetValue(PetscDMLabel,PetscInt,PetscInt*)
    PetscErrorCode DMLabelSetValue(PetscDMLabel,PetscInt,PetscInt)
    PetscErrorCode DMLabelClearValue(PetscDMLabel,PetscInt,PetscInt)
    PetscErrorCode DMLabelAddStratum(PetscDMLabel,PetscInt)
    PetscErrorCode DMLabelAddStrata(PetscDMLabel,PetscInt,const PetscInt[])
    PetscErrorCode DMLabelAddStrataIS(PetscDMLabel,PetscIS)
    PetscErrorCode DMLabelInsertIS(PetscDMLabel,PetscIS,PetscInt)
    PetscErrorCode DMLabelGetNumValues(PetscDMLabel,PetscInt*)

    PetscErrorCode DMLabelGetStratumBounds(PetscDMLabel,PetscInt,PetscInt*,PetscInt*)
    PetscErrorCode DMLabelGetValueIS(PetscDMLabel,PetscIS*)
    PetscErrorCode DMLabelStratumHasPoint(PetscDMLabel,PetscInt,PetscInt,PetscBool*)
    PetscErrorCode DMLabelHasStratum(PetscDMLabel,PetscInt,PetscBool*)
    PetscErrorCode DMLabelGetStratumSize(PetscDMLabel,PetscInt,PetscInt*)
    PetscErrorCode DMLabelGetStratumIS(PetscDMLabel,PetscInt,PetscIS*)
    PetscErrorCode DMLabelSetStratumIS(PetscDMLabel,PetscInt,PetscIS)
    PetscErrorCode DMLabelClearStratum(PetscDMLabel,PetscInt)

    PetscErrorCode DMLabelComputeIndex(PetscDMLabel)
    PetscErrorCode DMLabelCreateIndex(PetscDMLabel,PetscInt,PetscInt)
    PetscErrorCode DMLabelDestroyIndex(PetscDMLabel)
    PetscErrorCode DMLabelHasValue(PetscDMLabel,PetscInt,PetscBool*)
    PetscErrorCode DMLabelHasPoint(PetscDMLabel,PetscInt,PetscBool*)
    PetscErrorCode DMLabelGetBounds(PetscDMLabel,PetscInt*,PetscInt*)
    PetscErrorCode DMLabelFilter(PetscDMLabel,PetscInt,PetscInt)
    PetscErrorCode DMLabelPermute(PetscDMLabel,PetscIS,PetscDMLabel*)
    PetscErrorCode DMLabelDistribute(PetscDMLabel,PetscSF,PetscDMLabel*)
    PetscErrorCode DMLabelGather(PetscDMLabel,PetscSF,PetscDMLabel*)
    PetscErrorCode DMLabelConvertToSection(PetscDMLabel,PetscSection*,PetscIS*)
    PetscErrorCode DMLabelGetNonEmptyStratumValuesIS(PetscDMLabel, PetscIS*)