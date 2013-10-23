      SUBROUTINE SAVEINI
!
! WRITTEN BY YOUSU CHEN, PNNL, 03 JAN 2013
!
!------------DESCRIPTION------------------
! SAVE MODEL INITIAL INDICE
! 
!
      USE BUSMODULE, ONLY: BUS_I, BUS_I_SAVE,VMAX,VMIN
      USE GENMODULE, ONLY: GEN_STATUS, GEN_STATUS_SAVE
      USE BRCHMODULE
      USE MPI
      USE DEFDP

      IMPLICIT NONE

      BUS_I_SAVE=BUS_I
      F_BUS_SAVE=F_BUS
      T_BUS_SAVE=T_BUS
      GEN_STATUS_SAVE=GEN_STATUS
      BR_STATUS_SAVE=BR_STATUS
      VMAX = 1.1
      VMIN = 0.9

!      DO I = 1, NBRCH
!         CALL REDUCE(BR_ID2(I),BR_ID(I))
!      END DO
!      DO I = 1, NG
!         CALL REDUCE(GEN_ID2(I),GEN_ID(I))
!      END DO

      ENDSUBROUTINE