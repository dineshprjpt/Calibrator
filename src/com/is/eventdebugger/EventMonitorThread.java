package com.is.eventdebugger;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

import android.content.Context;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.util.Log;

public class EventMonitorThread extends Thread {

	// Navite Calls
	private native static int OpenTouchDevice(); // return number of devs

	public native static String GetDeviceInfo();

	private native static int PollDev();

	private native static int getType();

	private native static int getCode();

	private native static int getValue();

	public native static int SayCheese(String filename);

	public native static String GetGraphicsInfo();

	public native static int getSU();

	// Variables for Device Information and grabbing events
	static boolean m_bMonitorOn;
	int iTouchDeviceFileID;
	int x, y;
	int iTmpType, iTmpVal, iTmpCode;
	int iXMin, iXMax, iYMin, iYMax;
	int displayX, displayY;

	// Variables for parser
	boolean bDragOn;
	String strDragCommand;

	private boolean isDevicePrepared;

	// To communicate with android activities and other threads

	private Handler mHandler;
	private Bundle mBundle = new Bundle();
	private Context mContext;

	public EventMonitorThread(Handler handler, Context context) {
		mHandler = handler;
		mContext = context;
	}

	public void run() {
		bDragOn = false;
		m_bMonitorOn = true;
		x = y = -1;
		if (!PrepareDevices())
			return;
		while (m_bMonitorOn) {
			if (PollDev() == 0) {
				iTmpType = getType();
				iTmpVal = getValue();
				iTmpCode = getCode();
				//parse(iTmpType, iTmpCode, iTmpVal);
				strDragCommand += String.format("%04d,%04d,%04d", iTmpType, iTmpCode,
						iTmpVal);
				strDragCommand += "\n";
				Log.i(EventMonitorThread.class.toString(), strDragCommand);
				saveToFile(strDragCommand);
				// Store the command here during recording.
				Message msg = mHandler.obtainMessage();
				mBundle.putString("RESPONSE", strDragCommand);
				msg.setData(mBundle);
				mHandler.sendMessage(msg);
			}
		}
	}

	static {
		System.loadLibrary("EventMonitor");
	}

	private void saveToFile(String data) {

		try {
			String root = Environment.getExternalStorageDirectory().toString();
			File myDir = new File(root + "/EventDebugger");
			if (!myDir.exists()) {
				myDir.mkdirs();
			}
			File f = new File(myDir, "EventDebuggerLog.txt");
			FileOutputStream fos = new FileOutputStream(f, true);
			fos.write(data.getBytes());
			fos.close();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}

	}

	public boolean PrepareDevices() {
		iTouchDeviceFileID = OpenTouchDevice();
		if (iTouchDeviceFileID < 0) {
			return false;
		}

		/*String strDevInfo = GetDeviceInfo();
		saveToFile("This is Device Information from " + android.os.Build.MODEL + "\n");
		saveToFile(GetDeviceInfo());
		saveToFile("\n This is Graphics Device Information \n");
		saveToFile(GetGraphicsInfo());
		String token[] = strDevInfo.split(",");
		int i = 0;
		while (i < token.length) {
			InputDevice code = new InputDevice();
			code.ABS_TYPE = Integer.parseInt(token[i]);
			i++;
			code.ABS_MINIMUM = Integer.parseInt(token[i]);
			i++;
			code.ABS_MAXIMUM = Integer.parseInt(token[i]);
			i++;
			if (code.ABS_TYPE == 53) {
				iXMax = code.ABS_MAXIMUM;
				iXMin = code.ABS_MINIMUM;
			} else if (code.ABS_TYPE == 54) {
				iYMax = code.ABS_MAXIMUM;
				iYMin = code.ABS_MINIMUM;
			}
		}*/
		return true;

	}

	public void CloseAll() {
		m_bMonitorOn = false;
	}

	public void parse(int iType, int iCode, int iVal) {
		if (iType != 3 && iType != 0) {
			Log.i("", "Unknown type besized 0 and 3");
			return;

		}
		if (iType == 0 && iCode != 0) {
			Log.i("", "Type 0 but Code Not 0");
			return;
		}
		if (iCode == 57 && iVal >= 0) {
			if (bDragOn) {
				Log.i("", "Received Another Tracking ID while another is ON");
				return;
			}
			bDragOn = true;
			// not recording the tracking id for new logic
			strDragCommand = "";
			strDragCommand = String
					.format("%04d,%04d,%04d", iType, iCode, iVal);
			strDragCommand += "\n";
			saveToFile(strDragCommand);
		}
		if (iCode == 53) {
			if (!bDragOn) {
				Log.i("", "Received X without Tracking ID. Ignoring");
				return;
			}

			strDragCommand += String.format("%04d,%04d,%04d", iType, iCode,
					iVal);
			strDragCommand += "\n";
			saveToFile(strDragCommand);
			displayX = (iVal - iXMin) * Globals.width / (iXMax - iXMin + 1);

		}
		if (iCode == 54) {
			if (!bDragOn) {
				Log.i("", "Received Y without Tracking ID. Ignoring");
				return;
			}
			displayY = (iVal - iYMin) * Globals.height / (iYMax - iYMin + 1);
			strDragCommand += String.format("%04d,%04d,%04d", iType, iCode,
					iVal);
			strDragCommand += "\n";
			saveToFile(strDragCommand);

		}
		if (iCode == 57 && iVal == -1) {
			if (!bDragOn) {
				Log.i("", "Received Stop Tracking without starting. Ignoring");
				return;
			}
			bDragOn = false;
			// not storing the tracking id
			strDragCommand += String.format("%04d,%04d,%04d", iType, iCode,
					iVal);
			strDragCommand += "\n";
			saveToFile(strDragCommand);
			// Store the command here during recording.
			System.out.println(strDragCommand);
			Message msg = mHandler.obtainMessage();
			mBundle.putString("RESPONSE", strDragCommand);
			msg.setData(mBundle);
			mHandler.sendMessage(msg);

		}
	}
}