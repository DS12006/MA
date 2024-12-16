//****************************************************************************
//* FILE NAME   : itvreq.cpp                                                 *
//* VERSION     :                                                            *
//* ABSTRACT    : 周期受信要求処理                                           *
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
/* define文                                                                  */
/*****************************************************************************/
#define IR_CYCTIME                  10      //周期時間(秒)
#define IR_MEL_MADDR_DEV            484     //Mデバイス 偏差処理要求フラグ
#define IR_MEL_MADDR_DEVRESET       487     //Mデバイス 偏差リセット要求フラグ
#define IR_MEL_DADDR_IUNIT          6420    //Dデバイス Iユニット
#define IR_MEL_DADDR_DLYATUMI       6412    //Dデバイス 出側厚み
#define IR_MEL_DADDR_IUNITSDEV      6614    //Dデバイス Iユニット
#define IR_MEL_DADDR_DLYATUMISDEV   6566    //Dデバイス 出側厚み
#define IR_MEL_WADDR_DLYATUMI       0x0631  //Wデバイス 目標出側厚み
#define IR_IUNIT_BNPCNT             4       //Iユニット分布数
#define IR_DLYATUMI_BNPCNT          7       //出側厚み分布数

//アラームメッセージ
#define ALM_MSG11 "品質制御設定T書込異常"
/*****************************************************************************/
/* 構造体                                                                    */
/*****************************************************************************/
//MELSECアドレス情報
struct MELADR {
    SHORT   sDno;                       //デバイスNO
    SHORT   sRWDno;                     //先頭デバイスNO
    SHORT   sPos;                       //先頭デバイスNOからの位置
    SHORT   sRWByte;                    //読書込サイズ
};
//標準偏差・分布情報
struct IR_SDEV {
    FLOAT   fSDev;                      //標準偏差
    FLOAT   fBnp[IR_DLYATUMI_BNPCNT];   //分布
};
/*****************************************************************************/
/* 静的変数                                                                  */
/*****************************************************************************/
static struct MELADR sstMelSDevReq;     //要求フラグ アドレス情報
static SHORT         ssMelRSetDno;      //リセット要求デバイスNO
static SHORT         ssOffDt;           //OFF値
static LONG          snCount;           //統計処理回数
static FLOAT         sfIUnit[1000];     //Iユニット値
static FLOAT         sfDlyAtumi[1000];  //出側厚み値
/*****************************************************************************/
/* プロトタイプ宣言                                                          */
/*****************************************************************************/
LONG IRInit();
LONG IRMain();
LONG IRFQualitySet();
LONG IRSDev();
LONG IRSIUnit();
LONG IRSDlyAtumi();
//****************************************************************************
//* MODULE      :main                                                        *
//* ABSTRACT    :メイン処理                                                  *
//* RETURN      :                                                            *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
void main()
{
    LONG     retc;                       //戻り値

    //初期処理を行う
    retc = IRInit();
    if ( retc != 0 ) {
        ERRTRACE( "main", "IRInit", retc );
        xe_task_quit();
    }

    while ( 1 ) {

        //周期受信要求メイン処理を行う
        retc = IRMain();
        if ( retc < 0 ) {
            ERRTRACE( "main", "IRMain", retc );
            break;
        }

        //指定時間 WAITする
printf( "<Main> sleep!\n" );
        retc = task_delay( IR_CYCTIME );
        if ( retc == TASK_END_OK ) {
            break;
        }
    }

    //MELSEC NET 切断処理
    sub_mel_close();

    //ORACLE 切断処理
    sub_ora_close();

    //タスク終了処理
    task_stop();
    xe_task_quit();
}
//****************************************************************************
//* MODULE      :IRInit                                                      *
//* ABSTRACT    :周期受信要求初期処理                                        *
//* RETURN      :                                                            *
//*  0              正常                                                     *
//*  以外           異常                                                     *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRInit()
{
    LONG     retc;                       //戻り値

    xe_task_init( PRC_NAME_ITVREQ );

    //INIファイルを展開する
    retc = COMApplIni();
    if ( retc != 0 ) {
        ERRTRACE( "IRInit", "COMApplIni", retc );
        return( -1 );
    }

    //タスクチェック
    retc = task_chk();
    if (retc != TASK_START_NOMAL){
        ERRTRACE( "IRInit", "task_chk", retc );
        return( -2 );
    }

    //MELSEC NET 接続処理
    retc = COMMelInit();
    if ( retc != 0 ) {
        ERRTRACE( "STInit", "COMMelInit", retc );
        return( -3 );
    }

    //ORACLE 接続処理
    retc = COMOraInit();
    if ( retc != 0 ) {
        ERRTRACE( "IRInit", "COMOraInit", retc );
        return( -4 );
    }

    //要求フラグ読込時のデバイスNO等を求める
    sstMelSDevReq.sDno = IR_MEL_MADDR_DEV;
    sub_mel_BitDnogt( sstMelSDevReq.sDno, 4, 
                      &sstMelSDevReq.sRWDno, &sstMelSDevReq.sRWByte, &sstMelSDevReq.sPos );

    //標準偏差リセット要求フラグデバイスNOをセットする
    ssMelRSetDno = IR_MEL_MADDR_DEVRESET;

    //OFF値をセットする
    ssOffDt = 0;

    //初期化する
    snCount = 0;
    memset( sfIUnit, 0x00, sizeof(sfIUnit) );
    memset( sfDlyAtumi, 0x00, sizeof(sfDlyAtumi) );

    return( 0 );
}
//****************************************************************************
//* MODULE      :IRMain                                                      *
//* ABSTRACT    :周期受信要求メイン                                          *
//* RETURN      :                                                            *
//*  0              正常                                                     *
//*  以外           異常                                                     *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRMain()
{
    LONG    retc;                       //戻り値


/*+++ TEST 今はとりあえずコメントに 99/04/07+++++++++++++++++++++++++*
    //品質制御設定テーブルにPLC要求を登録する
    retc = IRFQualitySet();
    if ( retc != 0 ) {
        ERRTRACE( "IRMain", "IRFQualitySet", retc );
    }
*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

    //標準偏差計算を行う
    retc = IRSDev();
    if ( retc != 0 ) {
        ERRTRACE( "IRMain", "IRSDev", retc );
    }

    return( 0 );
}
//****************************************************************************
//* MODULE      :IRFQualitySet                                               *
//* ABSTRACT    :品質制御設定テーブル登録                                    *
//* RETURN      :                                                            *
//*  0              正常                                                     *
//*  以外           異常                                                     *
//* CREATE      :99-03-09 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRFQualitySet()
{
    CHAR    szTime[17+1];               //登録時刻
    CHAR    szNowTime[17+1];            //現在時刻(文字型)
    CHAR    szSql[1280];                //SQL文
    LONG    nNowTime[2];                //現在時刻(バイナリ時刻)
    LONG    nSelFlg;                    //selectデータ有無フラグ
    ULONG   unLen;                      //データサイズ
    LONG    retc;                       //戻り値

    struct  F_QUALITY stFQuality;       //品質制御設定情報

    /*<<< 品質制御設定テーブル(F_QUALITY)の1番古い日付のレコードを抽出する >>>*/

    //SQL文を編集する
    sprintf( szSql,"select QU_TIME from %s order by QU_TIME", ORA_FQUALITY_TNM );

    //selectを実行する
    retc = sub_ora_sqlexec( szSql );
    if ( retc != 0 ) {
        ERRTRACE( "IRFQualitySet", "sub_ora_sqlexec", retc );
        return( -1 );
    }

    //selectした情報を得る
    unLen = sizeof(szTime);
    nSelFlg = sub_ora_dataset( 1, szTime, &unLen );
    if ( nSelFlg != 0 ) { //未処理データが存在しない場合
        return( 1 );
    }

    /*<<< PLC要求レコードを登録する >>>*/

    //システム時刻を得る
    xe_time_get3( szNowTime, nNowTime, (INT *)&retc );

    //品質制御設定情報をクリアする
    memset( &stFQuality, 0x00, sizeof(struct F_QUALITY) );

    //PLC要求する項目をONする
/////    stFQuality.stCBulb[0].nRequest = 1;
/////    stFQuality.stCBulb[1].nRequest = 1;
/////    stFQuality.nRotorSuuR = 1;
/////    stFQuality.nRotorRitR = 1;

    //SQL文を編集する
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

    //updateを実行する
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
//* ABSTRACT    :標準偏差・分布処理                                          *
//* RETURN      :                                                            *
//*  0              正常                                                     *
//*  以外           異常                                                     *
//* CREATE      :99-04-07 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRSDev()
{
    SHORT   sFlag[32];                  //フラグ
    LONG    retc;                       //戻り値

    //要求フラグを読込む
    retc = sub_mel_rcv( DevM, sstMelSDevReq.sRWDno, sstMelSDevReq.sRWByte, sFlag );
    if ( retc != 0 ) {
        ERRTRACE( "TRMelGet", "sub_mel_rcv", retc );
        return( -1 );
    }

    //リセットフラグがONの場合、リセットする
    if ( sFlag[sstMelSDevReq.sPos+3] == 1 ) {
        snCount = 0;
        memset( sfIUnit, 0x00, sizeof(sfIUnit) );
        memset( sfDlyAtumi, 0x00, sizeof(sfDlyAtumi) );

       //リセットフラグをOFFする
        retc = sub_mel_Bitsnd( DevM, &ssMelRSetDno, (SHORT)1, &ssOffDt );
        if ( retc != 0 ) {
            ERRTRACE( "TRMelOnSnd", "sub_mel_Bitsnd", retc );
            return( -1 );
        }
    }

    //処理要求フラグがONの場合、標準偏差・分布処理を行う
    if ( sFlag[sstMelSDevReq.sPos] == 1 ) {

        //統計処理回数を加算する
        snCount++;

        //統計処理回数が1000を越えた場合は、1000にする
        if ( snCount >= 1000 ) {
            snCount = 1000;
        }

        //Iユニット処理を行う
        retc = IRSIUnit();
        if ( retc != 0 ) {
            ERRTRACE( "IRSDev", "IRSIUnit", retc );
            return( -2 );
        }
        
        //出側厚み処理を行う
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
//* ABSTRACT    :標準偏差・分布(Iユニット)処理                               *
//* RETURN      :                                                            *
//*  0              正常                                                     *
//*  以外           異常                                                     *
//* CREATE      :99-04-07 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRSIUnit()
{
    FLOAT   fIUnit;                     //Iユニット
    LONG    nBnp[IR_IUNIT_BNPCNT];      //分布
    DOUBLE  dIUnitAve;                  //Iユニット平均値
    DOUBLE  dWork;                      //フラグ
    DOUBLE  dIUnit1;                    //フラグ
    DOUBLE  dIUnit2;                    //フラグ
    DOUBLE  dIUnitSDev;                 //標準偏差
    DOUBLE  dIUnitBnp[IR_IUNIT_BNPCNT]; //分布
    LONG    i;                          //ループ変数
    LONG    retc;                       //戻り値
    struct  IR_SDEV  stIUnit;           //Iユニット情報

    //Iユニットを読込む
    retc = sub_mel_rcv( DevD, IR_MEL_DADDR_IUNIT, 4, (SHORT *)&fIUnit );
    if ( retc != 0 ) {
        ERRTRACE( "STSyukan", "sub_mel_rcv", retc );
        return( -1 );
    }
    sfIUnit[snCount-1] = fIUnit;

    /*
     * 標準偏差計算
     */

    //平均値を求める
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

    //Iユニット標準偏差を求める
    dWork = ( dIUnit1 - ( dIUnit2 * dIUnit2 / (DOUBLE)snCount ) ) / (DOUBLE)snCount;
    if ( dWork > 0.0 ) {
        dIUnitSDev = sqrt( dWork );
    } else {
        dIUnitSDev = 0.0;
	}
    stIUnit.fSDev = (FLOAT)dIUnitSDev;

    /*
     * 分布計算
     */

    //Iユニット分布を求める
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

    //MELSECにIユニット標準偏差・分布を書込む
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
//* ABSTRACT    :標準偏差・分布(出側厚み)処理                                *
//* RETURN      :                                                            *
//*  0              正常                                                     *
//*  以外           異常                                                     *
//* CREATE      :99-04-07 H.M                                                *
//* COPYRIGHT(C) 1998 MITSUBISHI ELECTRIC CORPORATION ALL RIGHT RESERVED     *
//****************************************************************************
LONG IRSDlyAtumi()
{
    FLOAT   fDlyAtumi;                  //出側厚み
    SHORT   sDlyAtumi;                  //目標出側厚み
    LONG    nBnp[IR_DLYATUMI_BNPCNT];   //分布
    LONG    i;                          //ループ変数
    LONG    retc;                       //戻り値
    DOUBLE  dWork;                      //フラグ
    DOUBLE  dDlyAtumi1;                    //フラグ
    DOUBLE  dDlyAtumi2;                    //フラグ
    DOUBLE  dDlyAtumiSDev;              //標準偏差
    DOUBLE  dDlyAtumiDev[1000];         //偏差値
    DOUBLE  dDlyAtumiBnp[IR_DLYATUMI_BNPCNT]; //分布
    struct  IR_SDEV  stDlyAtumi;        //出側厚み情報

    //出側厚みを読込む
    retc = sub_mel_rcv( DevD, IR_MEL_DADDR_DLYATUMI, 4, (SHORT *)&fDlyAtumi );
    if ( retc != 0 ) {
        ERRTRACE( "IRSDlyAtumi", "sub_mel_rcv", retc );
        return( -1 );
    }
    sfDlyAtumi[snCount-1] = fDlyAtumi;

    //目標出側厚みを読込む
    retc = sub_mel_rcv( DevW, IR_MEL_WADDR_DLYATUMI, 2, &sDlyAtumi );
    if ( retc != 0 ) {
        ERRTRACE( "IRSDlyAtumi", "sub_mel_rcv", retc );
        return( -2 );
    }

    /*
     * 分布計算
     */

    //出側厚み偏差値と分布を求める
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
     * 標準偏差計算
     */

    dDlyAtumi1 = 0.0;
    dDlyAtumi2 = 0.0;
    for ( i=0; i<snCount; i++ ) {
        dDlyAtumi1 = dDlyAtumi1 + ( dDlyAtumiDev[i] * dDlyAtumiDev[i] );
        dDlyAtumi2 = dDlyAtumi2 + dDlyAtumiDev[i];
    }

    //出側厚み標準偏差を求める
    dWork = ( dDlyAtumi1 - ( dDlyAtumi2 * dDlyAtumi2 / (DOUBLE)snCount ) ) / (DOUBLE)snCount;
    if ( dWork > 0.0 ) {
        dDlyAtumiSDev = sqrt( dWork );
	} else {
        dDlyAtumiSDev = 0.0;
	}
    stDlyAtumi.fSDev = (FLOAT)dDlyAtumiSDev;

    //MELSECに出側厚み標準偏差・分布を書込む
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
