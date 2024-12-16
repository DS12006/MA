//****************************************************************************
//* FILE NAME   : itvreq.cpp                                                 *
//* VERSION     :                                                            *
//* ABSTRACT    : ������M�v������                                           *
//* CREATE      : 99-03-09 H.M                                               *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <mdfunc2.h>
#include <math.h>

#include "COMDEF.h"
#include "COMORASUB.h"
#include "COMSUBFUNC.h"
#include "COMMELDEF.h"
#include "COMMELFUNC.h"

#include "apfrk.h"
#include "xe_time.h"
#include "sub_hist.h"
#include "xe_hist.h "
#include "xe_task.h "
#include "xe_addr.h "
/*****************************************************************************/
/* define��                                                                  */
/*****************************************************************************/
#define IR_CYCTIME                  10      //��������(�b)
#define IR_MEL_MADDR_DEV            484     //M�f�o�C�X �΍������v���t���O
#define IR_MEL_MADDR_DEVRESET       487     //M�f�o�C�X �΍����Z�b�g�v���t���O
#define IR_MEL_DADDR_IUNIT          6420    //D�f�o�C�X I���j�b�g
#define IR_MEL_DADDR_DLYATUMI       6412    //D�f�o�C�X �o������
#define IR_MEL_DADDR_IUNITSDEV      6614    //D�f�o�C�X I���j�b�g
#define IR_MEL_DADDR_DLYATUMISDEV   6566    //D�f�o�C�X �o������
#define IR_MEL_WADDR_DLYATUMI       0x0631  //W�f�o�C�X �ڕW�o������
#define IR_IUNIT_BNPCNT             4       //I���j�b�g���z��
#define IR_DLYATUMI_BNPCNT          7       //�o�����ݕ��z��

//�A���[�����b�Z�[�W
#define ALM_MSG11 "�i������ݒ�T�����ُ�"
/*****************************************************************************/
/* �\����                                                                    */
/*****************************************************************************/
//MELSEC�A�h���X���
struct MELADR {
    SHORT   sDno;                       //�f�o�C�XNO
    SHORT   sRWDno;                     //�擪�f�o�C�XNO
    SHORT   sPos;                       //�擪�f�o�C�XNO����̈ʒu
    SHORT   sRWByte;                    //�Ǐ����T�C�Y
};
//�W���΍��E���z���
struct IR_SDEV {
    FLOAT   fSDev;                      //�W���΍�
    FLOAT   fBnp[IR_DLYATUMI_BNPCNT];   //���z
};
/*****************************************************************************/
/* �ÓI�ϐ�                                                                  */
/*****************************************************************************/
static struct MELADR sstMelSDevReq;     //�v���t���O �A�h���X���
static SHORT         ssMelRSetDno;      //���Z�b�g�v���f�o�C�XNO
static SHORT         ssOffDt;           //OFF�l
static LONG          snCount;           //���v������
static FLOAT         sfIUnit[1000];     //I���j�b�g�l
static FLOAT         sfDlyAtumi[1000];  //�o�����ݒl
/*****************************************************************************/
/* �v���g�^�C�v�錾                                                          */
/*****************************************************************************/
LONG IRInit();
LONG IRMain();
LONG IRFQualitySet();
LONG IRSDev();
LONG IRSIUnit();
LONG IRSDlyAtumi();
//****************************************************************************
//* MODULE      :main                                                        *
//* ABSTRACT    :���C������                                                  *
//* RETURN      :                                                            *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
void main()
{
    LONG     retc;                       //�߂�l

    //�����������s��
    retc = IRInit();
    if ( retc != 0 ) {
        ERRTRACE( "main", "IRInit", retc );
        xe_task_quit();
    }

    while ( 1 ) {

        //������M�v�����C���������s��
        retc = IRMain();
        if ( retc < 0 ) {
            ERRTRACE( "main", "IRMain", retc );
            break;
        }

        //�w�莞�� WAIT����
printf( "<Main> sleep!\n" );
        retc = task_delay( IR_CYCTIME );
        if ( retc == TASK_END_OK ) {
            break;
        }
    }

    //MELSEC NET �ؒf����
    sub_mel_close();

    //ORACLE �ؒf����
    sub_ora_close();

    //�^�X�N�I������
    task_stop();
    xe_task_quit();
}
//****************************************************************************
//* MODULE      :IRInit                                                      *
//* ABSTRACT    :������M�v����������                                        *
//* RETURN      :                                                            *
//*  0              ����                                                     *
//*  �ȊO           �ُ�                                                     *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRInit()
{
    LONG     retc;                       //�߂�l

    xe_task_init( PRC_NAME_ITVREQ );

    //INI�t�@�C����W�J����
    retc = COMApplIni();
    if ( retc != 0 ) {
        ERRTRACE( "IRInit", "COMApplIni", retc );
        return( -1 );
    }

    //�^�X�N�`�F�b�N
    retc = task_chk();
    if (retc != TASK_START_NOMAL){
        ERRTRACE( "IRInit", "task_chk", retc );
        return( -2 );
    }

    //MELSEC NET �ڑ�����
    retc = COMMelInit();
    if ( retc != 0 ) {
        ERRTRACE( "STInit", "COMMelInit", retc );
        return( -3 );
    }

    //ORACLE �ڑ�����
    retc = COMOraInit();
    if ( retc != 0 ) {
        ERRTRACE( "IRInit", "COMOraInit", retc );
        return( -4 );
    }

    //�v���t���O�Ǎ����̃f�o�C�XNO�������߂�
    sstMelSDevReq.sDno = IR_MEL_MADDR_DEV;
    sub_mel_BitDnogt( sstMelSDevReq.sDno, 4, 
                      &sstMelSDevReq.sRWDno, &sstMelSDevReq.sRWByte, &sstMelSDevReq.sPos );

    //�W���΍����Z�b�g�v���t���O�f�o�C�XNO���Z�b�g����
    ssMelRSetDno = IR_MEL_MADDR_DEVRESET;

    //OFF�l���Z�b�g����
    ssOffDt = 0;

    //����������
    snCount = 0;
    memset( sfIUnit, 0x00, sizeof(sfIUnit) );
    memset( sfDlyAtumi, 0x00, sizeof(sfDlyAtumi) );

    return( 0 );
}
//****************************************************************************
//* MODULE      :IRMain                                                      *
//* ABSTRACT    :������M�v�����C��                                          *
//* RETURN      :                                                            *
//*  0              ����                                                     *
//*  �ȊO           �ُ�                                                     *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRMain()
{
    LONG    retc;                       //�߂�l


/*+++ TEST ���͂Ƃ肠�����R�����g�� 99/04/07+++++++++++++++++++++++++*
    //�i������ݒ�e�[�u����PLC�v����o�^����
    retc = IRFQualitySet();
    if ( retc != 0 ) {
        ERRTRACE( "IRMain", "IRFQualitySet", retc );
    }
*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

    //�W���΍��v�Z���s��
    retc = IRSDev();
    if ( retc != 0 ) {
        ERRTRACE( "IRMain", "IRSDev", retc );
    }

    return( 0 );
}
//****************************************************************************
//* MODULE      :IRFQualitySet                                               *
//* ABSTRACT    :�i������ݒ�e�[�u���o�^                                    *
//* RETURN      :                                                            *
//*  0              ����                                                     *
//*  �ȊO           �ُ�                                                     *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRFQualitySet()
{
    CHAR    szTime[17+1];               //�o�^����
    CHAR    szNowTime[17+1];            //���ݎ���(�����^)
    CHAR    szSql[1280];                //SQL��
    LONG    nNowTime[2];                //���ݎ���(�o�C�i������)
    LONG    nSelFlg;                    //select�f�[�^�L���t���O
    ULONG   unLen;                      //�f�[�^�T�C�Y
    LONG    retc;                       //�߂�l

    struct  F_QUALITY stFQuality;       //�i������ݒ���

    /*<<< �i������ݒ�e�[�u��(F_QUALITY)��1�ԌÂ����t�̃��R�[�h�𒊏o���� >>>*/

    //SQL����ҏW����
    sprintf( szSql,"select QU_TIME from %s order by QU_TIME", ORA_FQUALITY_TNM );

    //select�����s����
    retc = sub_ora_sqlexec( szSql );
    if ( retc != 0 ) {
        ERRTRACE( "IRFQualitySet", "sub_ora_sqlexec", retc );
        return( -1 );
    }

    //select�������𓾂�
    unLen = sizeof(szTime);
    nSelFlg = sub_ora_dataset( 1, szTime, &unLen );
    if ( nSelFlg != 0 ) { //�������f�[�^�����݂��Ȃ��ꍇ
        return( 1 );
    }

    /*<<< PLC�v�����R�[�h��o�^���� >>>*/

    //�V�X�e�������𓾂�
    xe_time_get3( szNowTime, nNowTime, (INT *)&retc );

    //�i������ݒ�����N���A����
    memset( &stFQuality, 0x00, sizeof(struct F_QUALITY) );

    //PLC�v�����鍀�ڂ�ON����
/////    stFQuality.stCBulb[0].nRequest = 1;
/////    stFQuality.stCBulb[1].nRequest = 1;
/////    stFQuality.nRotorSuuR = 1;
/////    stFQuality.nRotorRitR = 1;

    //SQL����ҏW����
    sprintf( szSql, "update %s set "
                    "QU_TIME='%-17.17s',QU_STATUS=1,"
                    "QU_HAKUWIDTH_R=%d,QU_HAKUWIDTH=%d,"
                    "QU_VCRLFSET1_R=%d,QU_VCRLFSET1=%d,"
                    "QU_VCRLFSET2_R=%d,QU_VCRLFSET2=%d,"
                    "QU_WRBRLFSET1_R=%d,QU_WRBRLFSET1=%d,"
                    "QU_WRBRLFSET2_R=%d,QU_WRBRLFSET2=%d,"
                    "QU_EPRTHOSEINO_R=%d,QU_EPRTHOSEINO=%d,"
                    "QU_GALFRMFK_R=%d,QU_GALFRMFK=%d,"
                    "QU_GALBUILDK_R=%d,QU_GALBUILDK=%d,"
                    "QU_KBAND1_R=%d,QU_KBAND1=%d,"
                    "QU_KBAND2_R=%d,QU_KBAND2=%d,"
                    "QU_ATENMODE_R=%d,QU_ATENMODE=%d,"
                    "QU_BRDKHR_R=%d,QU_BRDKHR=%d,"
                    "QU_ATENLADSET_R=%d,QU_ATENLADSET=%d,"
                    "QU_SOSEIK1_R=%d,QU_SOSEIK1=%d,"
                    "QU_SOSEIK2_R=%d,QU_SOSEIK2=%d,"
                    "QU_SOSEIK3_R=%d,QU_SOSEIK3=%d,"
                    "QU_GAPSET_R=%d,QU_GAPSET=%d,"
                    "QU_AFCCRLWIDTH_R=%d,QU_AFCCRLWIDTH=%d,"
                    "QU_YOBI1_R=%d,QU_YOBI1=%d,"
                    "QU_YOBI2_R=%d,QU_YOBI2=%d,"
                    "QU_YOBI3_R=%d,QU_YOBI3=%d,"
                    "QU_WRKEI_R=%d,QU_WRKEI=%d,"
                    "QU_BURKEI_R=%d,QU_BURKEI=%d,"
                    "QU_VCROLLKEI_R=%d,QU_VCROLLKEI=%d,"
                    "QU_GAPWS_R=%d,QU_GAPWS=%d,"
                    "QU_GAPDS_R=%d,QU_GAPDS=%d,"
                    "QU_MILLCONS_R=%d,QU_MILLCONS=%d,"
                    "QU_CBULB1_R=%d,QU_CBULB1=%d,"
                    "QU_CBULB2_R=%d,QU_CBULB2=%d,"
                    "QU_ROTORSUU_R=%d,QU_ROTORSUU=%d,"
                    "QU_ROTORRIT_R=%d,QU_ROTORRIT=%d,"
                    "QU_ITAATU_R=%d,QU_ITAATU=%d,"
                    "QU_HOSEI_R=%d,QU_HOSEI=%d,"
                    "QU_YOBI4_R=%d,QU_YOBI4=%d,"
                    "QU_YOBI5_R=%d,QU_YOBI5=%d,"
                    "QU_YOBI6_R=%d,QU_YOBI6=%d "
                    "where QU_TIME='%-17.17s'",
                ORA_FQUALITY_TNM,
                szNowTime,
                stFQuality.nHakuWidthR, stFQuality.nHakuWidth,
                stFQuality.stVCRlFSet[0].nRequest, stFQuality.stVCRlFSet[0].nData,
                stFQuality.stVCRlFSet[1].nRequest, stFQuality.stVCRlFSet[1].nData,
                stFQuality.stWRBRlFSet[0].nRequest, stFQuality.stWRBRlFSet[0].nData,
                stFQuality.stWRBRlFSet[1].nRequest, stFQuality.stWRBRlFSet[1].nData,
                stFQuality.nEPrtHoseiNoR, stFQuality.nEPrtHoseiNo,
                stFQuality.nGalFrmFKR, stFQuality.nGalFrmFK,
                stFQuality.nGalBuildKR, stFQuality.nGalBuildK,
                stFQuality.stKBand[0].nRequest, stFQuality.stKBand[0].nData,
                stFQuality.stKBand[1].nRequest, stFQuality.stKBand[1].nData,
                stFQuality.nAtenModeR, stFQuality.nAtenMode,
                stFQuality.nBrdKHrR, stFQuality.nBrdKHr,
                stFQuality.nAtenLadSetR, stFQuality.nAtenLadSet,
                stFQuality.stSoseiK[0].nRequest, stFQuality.stSoseiK[0].nData,
                stFQuality.stSoseiK[1].nRequest, stFQuality.stSoseiK[1].nData,
                stFQuality.stSoseiK[2].nRequest, stFQuality.stSoseiK[2].nData,
                stFQuality.nGapSetR, stFQuality.nGapSet,
                stFQuality.nAFCCrlWidthR, stFQuality.nAFCCrlWidth,
                stFQuality.nYobi1R, stFQuality.nYobi1,
                stFQuality.nYobi2R, stFQuality.nYobi2,
                stFQuality.nYobi3R, stFQuality.nYobi3,
                stFQuality.nWRKeiR, stFQuality.nWRKei,
                stFQuality.nBURKeiR, stFQuality.nBURKei,
                stFQuality.nVCRollKeiR, stFQuality.nVCRollKei,
                stFQuality.nGapWsR, stFQuality.nGapWs,
                stFQuality.nGapDsR, stFQuality.nGapDs,
                stFQuality.nMillConsR, stFQuality.nMillCons,
                stFQuality.stCBulb[0].nRequest, stFQuality.stCBulb[0].nData,
                stFQuality.stCBulb[1].nRequest, stFQuality.stCBulb[1].nData,
                stFQuality.nRotorSuuR, stFQuality.nRotorSuu,
                stFQuality.nRotorRitR, stFQuality.nRotorRit,
                stFQuality.nItaAtuR, stFQuality.nItaAtu,
                stFQuality.nHoseiR, stFQuality.nHosei,
                stFQuality.nYobi4R, stFQuality.nYobi4,
                stFQuality.nYobi5R, stFQuality.nYobi5,
                stFQuality.nYobi6R, stFQuality.nYobi6,
                szTime );

//printf( "SQL:[%s]\n", szSql );

    //update�����s����
    retc = sub_ora_sqlexec( szSql );
    if ( retc != 0 ) {
        ERRTRACE( "IRFQualitySet", "sub_ora_sqlexec", retc );
        COMAlarmOut( ALM_OUTKND_ALL, ALM_ALMKND_ERROR, ALM_MSG11 );
        return( -2 );
    }
    return( 0 );
}
//****************************************************************************
//* MODULE      :IRSDev                                                      *
//* ABSTRACT    :�W���΍��E���z����                                          *
//* RETURN      :                                                            *
//*  0              ����                                                     *
//*  �ȊO           �ُ�                                                     *
//* CREATE      :99-04-07 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRSDev()
{
    SHORT   sFlag[32];                  //�t���O
    LONG    retc;                       //�߂�l

    //�v���t���O��Ǎ���
    retc = sub_mel_rcv( DevM, sstMelSDevReq.sRWDno, sstMelSDevReq.sRWByte, sFlag );
    if ( retc != 0 ) {
        ERRTRACE( "TRMelGet", "sub_mel_rcv", retc );
        return( -1 );
    }

    //���Z�b�g�t���O��ON�̏ꍇ�A���Z�b�g����
    if ( sFlag[sstMelSDevReq.sPos+3] == 1 ) {
        snCount = 0;
        memset( sfIUnit, 0x00, sizeof(sfIUnit) );
        memset( sfDlyAtumi, 0x00, sizeof(sfDlyAtumi) );

       //���Z�b�g�t���O��OFF����
        retc = sub_mel_Bitsnd( DevM, &ssMelRSetDno, (SHORT)1, &ssOffDt );
        if ( retc != 0 ) {
            ERRTRACE( "TRMelOnSnd", "sub_mel_Bitsnd", retc );
            return( -1 );
        }
    }

    //�����v���t���O��ON�̏ꍇ�A�W���΍��E���z�������s��
    if ( sFlag[sstMelSDevReq.sPos] == 1 ) {

        //���v�����񐔂����Z����
        snCount++;

        //���v�����񐔂�1000���z�����ꍇ�́A1000�ɂ���
        if ( snCount >= 1000 ) {
            snCount = 1000;
        }

        //I���j�b�g�������s��
        retc = IRSIUnit();
        if ( retc != 0 ) {
            ERRTRACE( "IRSDev", "IRSIUnit", retc );
            return( -2 );
        }
        
        //�o�����ݏ������s��
        retc = IRSDlyAtumi();
        if ( retc != 0 ) {
            ERRTRACE( "IRSDev", "IRSDlyAtumi", retc );
            return( -3 );
        }
    }

    return( 0 );
}
//****************************************************************************
//* MODULE      :IRSIUnit                                                    *
//* ABSTRACT    :�W���΍��E���z(I���j�b�g)����                               *
//* RETURN      :                                                            *
//*  0              ����                                                     *
//*  �ȊO           �ُ�                                                     *
//* CREATE      :99-04-07 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRSIUnit()
{
    FLOAT   fIUnit;                     //I���j�b�g
    LONG    nBnp[IR_IUNIT_BNPCNT];      //���z
    DOUBLE  dIUnitAve;                  //I���j�b�g���ϒl
    DOUBLE  dWork;                      //�t���O
    DOUBLE  dIUnit1;                    //�t���O
    DOUBLE  dIUnit2;                    //�t���O
    DOUBLE  dIUnitSDev;                 //�W���΍�
    DOUBLE  dIUnitBnp[IR_IUNIT_BNPCNT]; //���z
    LONG    i;                          //���[�v�ϐ�
    LONG    retc;                       //�߂�l
    struct  IR_SDEV  stIUnit;           //I���j�b�g���

    //I���j�b�g��Ǎ���
    retc = sub_mel_rcv( DevD, IR_MEL_DADDR_IUNIT, 4, (SHORT *)&fIUnit );
    if ( retc != 0 ) {
        ERRTRACE( "STSyukan", "sub_mel_rcv", retc );
        return( -1 );
    }
    sfIUnit[snCount-1] = fIUnit;

    /*
     * �W���΍��v�Z
     */

    //���ϒl�����߂�
    dIUnitAve = 0.0;
    for ( i=0; i<snCount; i++ ) {
        dIUnitAve = dIUnitAve + sfIUnit[i];
    }
    dIUnitAve = dIUnitAve / (DOUBLE)snCount;

    dIUnit1 = 0.0;
    dIUnit2 = 0.0;
    for ( i=0; i<snCount; i++ ) {
        if ( dIUnitAve != 0.0 ) {
            dWork = ( ( sfIUnit[i] - dIUnitAve ) / dIUnitAve ) * 100.0;
        } else {
            dWork = 0.0;
        }
        dIUnit1 = dIUnit1 + ( dWork * dWork );
        dIUnit2 = dIUnit2 +  dWork;
    }

    //I���j�b�g�W���΍������߂�
    dWork = ( dIUnit1 - ( dIUnit2 * dIUnit2 / (DOUBLE)snCount ) ) / (DOUBLE)snCount;
    if ( dWork > 0.0 ) {
        dIUnitSDev = sqrt( dWork );
    } else {
        dIUnitSDev = 0.0;
	}
    stIUnit.fSDev = (FLOAT)dIUnitSDev;

    /*
     * ���z�v�Z
     */

    //I���j�b�g���z�����߂�
    memset( nBnp, 0x00, sizeof(nBnp) );
    for ( i=0; i<snCount; i++ ) {

        if ( sfIUnit[i] <= 10.0 ) {
            nBnp[0]++;
        } else if ( sfIUnit[i] > 10.0 && sfIUnit[i] <= 20.0 ) {
            nBnp[1]++;
        } else if ( sfIUnit[i] > 20.0 && sfIUnit[i] <= 50.0 ) {
            nBnp[2]++;
        } else if ( sfIUnit[i] > 50.0 ) {
            nBnp[3]++;
        }
    }
    for ( i=0; i<IR_IUNIT_BNPCNT; i++ ) {
        dIUnitBnp[i] = (DOUBLE)nBnp[i] / (DOUBLE)snCount * 100.0;
        stIUnit.fBnp[i] = (FLOAT)dIUnitBnp[i];
    }

    //MELSEC��I���j�b�g�W���΍��E���z��������
    retc = sub_mel_sndI( DevD, IR_MEL_DADDR_IUNITSDEV,
                         (1+IR_IUNIT_BNPCNT)*4, (SHORT *)&stIUnit );
    if ( retc != 0 ) {
        ERRTRACE( "IRSIUnit", "sub_mel_sndI", retc );
        return( -2 );
    }

    return( 0 );
}
//****************************************************************************
//* MODULE      :IRSDlyAtumi                                                 *
//* ABSTRACT    :�W���΍��E���z(�o������)����                                *
//* RETURN      :                                                            *
//*  0              ����                                                     *
//*  �ȊO           �ُ�                                                     *
//* CREATE      :99-04-07 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRSDlyAtumi()
{
    FLOAT   fDlyAtumi;                  //�o������
    SHORT   sDlyAtumi;                  //�ڕW�o������
    LONG    nBnp[IR_DLYATUMI_BNPCNT];   //���z
    LONG    i;                          //���[�v�ϐ�
    LONG    retc;                       //�߂�l
    DOUBLE  dWork;                      //�t���O
    DOUBLE  dDlyAtumi1;                    //�t���O
    DOUBLE  dDlyAtumi2;                    //�t���O
    DOUBLE  dDlyAtumiSDev;              //�W���΍�
    DOUBLE  dDlyAtumiDev[1000];         //�΍��l
    DOUBLE  dDlyAtumiBnp[IR_DLYATUMI_BNPCNT]; //���z
    struct  IR_SDEV  stDlyAtumi;        //�o�����ݏ��

    //�o�����݂�Ǎ���
    retc = sub_mel_rcv( DevD, IR_MEL_DADDR_DLYATUMI, 4, (SHORT *)&fDlyAtumi );
    if ( retc != 0 ) {
        ERRTRACE( "IRSDlyAtumi", "sub_mel_rcv", retc );
        return( -1 );
    }
    sfDlyAtumi[snCount-1] = fDlyAtumi;

    //�ڕW�o�����݂�Ǎ���
    retc = sub_mel_rcv( DevW, IR_MEL_WADDR_DLYATUMI, 2, &sDlyAtumi );
    if ( retc != 0 ) {
        ERRTRACE( "IRSDlyAtumi", "sub_mel_rcv", retc );
        return( -2 );
    }

    /*
     * ���z�v�Z
     */

    //�o�����ݕ΍��l�ƕ��z�����߂�
    memset( nBnp, 0x00, sizeof(nBnp) );
    for ( i=0; i<snCount; i++ ) {

        if ( sDlyAtumi != 0 ) {
            dDlyAtumiDev[i] = ( (DOUBLE)sfDlyAtumi[i] - (DOUBLE)sDlyAtumi ) * 100.0 / 
                                (DOUBLE)sDlyAtumi;
        } else {
            dDlyAtumiDev[i] = 0.0;
        }

        if ( dDlyAtumiDev[i] <= -5.0 ) {
            nBnp[0]++;
        } else if ( dDlyAtumiDev[i] > -5.0 && dDlyAtumiDev[i] <= -3.0 ) {
            nBnp[1]++;
        } else if ( dDlyAtumiDev[i] > -3.0 && dDlyAtumiDev[i] <= -1.0 ) {
            nBnp[2]++;
        } else if ( dDlyAtumiDev[i] > -1.0 && dDlyAtumiDev[i] < 1.0 ) {
            nBnp[3]++;
        } else if ( dDlyAtumiDev[i] >= 1.0 && dDlyAtumiDev[i] < 3.0 ) {
            nBnp[4]++;
        } else if ( dDlyAtumiDev[i] >= 3.0 && dDlyAtumiDev[i] < 5.0 ) {
            nBnp[5]++;
        } else if ( dDlyAtumiDev[i] >= 5.0 ) {
            nBnp[6]++;
        }
    }
    for ( i=0; i<IR_DLYATUMI_BNPCNT; i++ ) {
        dDlyAtumiBnp[i] = (DOUBLE)nBnp[i] / (DOUBLE)snCount * 100.0;
        stDlyAtumi.fBnp[i] = (FLOAT)dDlyAtumiBnp[i];
    }

    /*
     * �W���΍��v�Z
     */

    dDlyAtumi1 = 0.0;
    dDlyAtumi2 = 0.0;
    for ( i=0; i<snCount; i++ ) {
        dDlyAtumi1 = dDlyAtumi1 + ( dDlyAtumiDev[i] * dDlyAtumiDev[i] );
        dDlyAtumi2 = dDlyAtumi2 + dDlyAtumiDev[i];
    }

    //�o�����ݕW���΍������߂�
    dWork = ( dDlyAtumi1 - ( dDlyAtumi2 * dDlyAtumi2 / (DOUBLE)snCount ) ) / (DOUBLE)snCount;
    if ( dWork > 0.0 ) {
        dDlyAtumiSDev = sqrt( dWork );
	} else {
        dDlyAtumiSDev = 0.0;
	}
    stDlyAtumi.fSDev = (FLOAT)dDlyAtumiSDev;

    //MELSEC�ɏo�����ݕW���΍��E���z��������
    retc = sub_mel_sndI( DevD, IR_MEL_DADDR_DLYATUMISDEV,
                         (1+IR_DLYATUMI_BNPCNT)*4, (SHORT *)&stDlyAtumi );
/*+++*/
printf( "cnt:[%d] sdly:[%d] dD1:[%f] dD2:[%f] dwk:[%f]\n",
	   snCount, sDlyAtumi, dDlyAtumi1, dDlyAtumi2, dWork );
/*+++*/
    if ( retc != 0 ) {
        ERRTRACE( "IRSDlyAtumi", "sub_mel_sndI", retc );
        return( -3 );
    }

    return( 0 );
}
