// CDiscTrackInfo.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include "pch.h"
#include <iostream>

#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#include <ntddscsi.h>
#include <stddef.h>

// 各トラックの開始位置を 秒 で格納する
int get_pos_min(unsigned int* pos)
{
	HANDLE fh;
	DWORD ioctl_bytes;
	BOOL ioctl_rv;

	// READ TOC (MSF) MSF: Minute, Second, Frame
	const unsigned char cdb[] = { 0x43, 2, 0x00, 0, 0, 0, 0, 4, 0, 0x00, 0, 0 };

	UCHAR buf[2352];
	struct sptd_with_sense
	{
		SCSI_PASS_THROUGH_DIRECT s;
		UCHAR sense[128];
	} sptd;

	fh = CreateFileW(L"\\\\.\\F:", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);

	memset(&sptd, 0, sizeof(sptd));
	sptd.s.Length = sizeof(sptd.s);
	sptd.s.CdbLength = sizeof(cdb);
	sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.s.TimeOutValue = 30;
	sptd.s.DataBuffer = buf;
	sptd.s.DataTransferLength = sizeof(buf);
	sptd.s.SenseInfoLength = sizeof(sptd.sense);
	sptd.s.SenseInfoOffset = offsetof(struct sptd_with_sense, sense);
	memcpy(sptd.s.Cdb, cdb, sizeof(cdb));

	ioctl_rv = DeviceIoControl(fh, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd,
		sizeof(sptd), &sptd, sizeof(sptd), &ioctl_bytes, NULL);

	// buf[0]  0x11 -> pre emphasis      0x01 -> no emphasis

	CloseHandle(fh);

	int i = 0;
	for (i = 0; i < buf[3]; ++i)
	{
		
		unsigned int a =
			(unsigned int)buf[8 + i * 8 + 1] * 60 +
			(unsigned int)buf[8 + i * 8 + 2];
		pos[i] = a;
	}
	return i;
}

int get_pos(unsigned int* pos)
{
	HANDLE fh;
	DWORD ioctl_bytes;
	BOOL ioctl_rv;

	// READ TOC (LBA)
	const unsigned char cdb[] = { 0x43, 0, 0x00, 0, 0, 0, 0, 4, 0, 0x00, 0, 0 };

	UCHAR buf[2352];
	struct sptd_with_sense
	{
		SCSI_PASS_THROUGH_DIRECT s;
		UCHAR sense[128];
	} sptd;

	fh = CreateFileW(L"\\\\.\\F:", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);

	memset(&sptd, 0, sizeof(sptd));
	sptd.s.Length = sizeof(sptd.s);
	sptd.s.CdbLength = sizeof(cdb);
	sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.s.TimeOutValue = 30;
	sptd.s.DataBuffer = buf;
	sptd.s.DataTransferLength = sizeof(buf);
	sptd.s.SenseInfoLength = sizeof(sptd.sense);
	sptd.s.SenseInfoOffset = offsetof(struct sptd_with_sense, sense);
	memcpy(sptd.s.Cdb, cdb, sizeof(cdb));

	ioctl_rv = DeviceIoControl(fh, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd,
		sizeof(sptd), &sptd, sizeof(sptd), &ioctl_bytes, NULL);

	// buf[0]  0x11 -> pre emphasis      0x01 -> no emphasis

	CloseHandle(fh);

	int i = 0;
	for (i = 0; i < buf[3]; ++i)
	{
		unsigned int a =
			(unsigned int)buf[8 + i * 8] << 24 |
			(unsigned int)buf[8 + i * 8 +1] << 16 |
			(unsigned int)buf[8 + i * 8 + 2] << 8 |
			(unsigned int)buf[8 + i * 8 + 3];
		pos[i] = a;
	}
	return i;
}

int check(unsigned int pos)
{
	HANDLE fh;
	DWORD ioctl_bytes;
	BOOL ioctl_rv;

	// READ CD (0xBE)  第1セクタから1セクタ読み出す　010 = Qサブチャネルデータ 
	unsigned char cdb[] = { 0xBE, 0, 0, 0, 0, 0/*pos*/, 0, 0, 1, 0x00, 2, 0 }; // 動作確認済み
	unsigned char* p = (unsigned char*)&pos;
	cdb[2] = p[3];
	cdb[3] = p[2];
	cdb[4] = p[1];
	cdb[5] = p[0];

	UCHAR buf[2352];
	struct sptd_with_sense
	{
		SCSI_PASS_THROUGH_DIRECT s;
		UCHAR sense[128];
	} sptd;

	fh = CreateFileW(L"\\\\.\\F:", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);

	memset(&sptd, 0, sizeof(sptd));
	sptd.s.Length = sizeof(sptd.s);
	sptd.s.CdbLength = sizeof(cdb);
	sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.s.TimeOutValue = 30;
	sptd.s.DataBuffer = buf;
	sptd.s.DataTransferLength = sizeof(buf);
	sptd.s.SenseInfoLength = sizeof(sptd.sense);
	sptd.s.SenseInfoOffset = offsetof(struct sptd_with_sense, sense);
	memcpy(sptd.s.Cdb, cdb, sizeof(cdb));

	ioctl_rv = DeviceIoControl(fh, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd,
		sizeof(sptd), &sptd, sizeof(sptd), &ioctl_bytes, NULL);

	// buf[0]  0x11 -> pre emphasis      0x01 -> no emphasis

	CloseHandle(fh);

	if (buf[0] & 0x10) return 1;

	return 0;
}

int main()
{
    //std::cout << "Hello World!\n"; 
	unsigned int pos[128];
	unsigned int pos_min[128];
	int num = 	get_pos(pos);
	get_pos_min(pos_min);
	for (int i = 0; i < num; ++i)
	{
		std::cout << check(pos[i]) << "   " << pos_min[i];
		std::cout << "\n"; 
	}

}

// プログラムの実行: Ctrl + F5 または [デバッグ] > [デバッグなしで開始] メニュー
// プログラムのデバッグ: F5 または [デバッグ] > [デバッグの開始] メニュー

// 作業を開始するためのヒント: 
//    1. ソリューション エクスプローラー ウィンドウを使用してファイルを追加/管理します 
//   2. チーム エクスプローラー ウィンドウを使用してソース管理に接続します
//   3. 出力ウィンドウを使用して、ビルド出力とその他のメッセージを表示します
//   4. エラー一覧ウィンドウを使用してエラーを表示します
//   5. [プロジェクト] > [新しい項目の追加] と移動して新しいコード ファイルを作成するか、[プロジェクト] > [既存の項目の追加] と移動して既存のコード ファイルをプロジェクトに追加します
//   6. 後ほどこのプロジェクトを再び開く場合、[ファイル] > [開く] > [プロジェクト] と移動して .sln ファイルを選択します
