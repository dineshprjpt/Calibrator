package com.is.eventdebugger;

import java.io.File;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.util.DisplayMetrics;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnTouchListener;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

@SuppressLint("HandlerLeak")
public class EventDebugger extends Activity {

	private EventMonitorThread mEventMonitorThread;
	private TextView mInst;
	private Paint mPaint;

	private LinearLayout mMainLayout;
	
	private MyView mMyView;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.main);
		mMainLayout = (LinearLayout) findViewById(R.id.mainlayout);
		getResolution();
		EventMonitorThread.getSU();
		setupViews();
		startMonitor();
		mPaint = new Paint();
		mPaint.setAntiAlias(true);
		mPaint.setDither(true);
		mPaint.setColor(Color.RED);
		mPaint.setStyle(Paint.Style.STROKE);
		mPaint.setStrokeJoin(Paint.Join.ROUND);
		mPaint.setStrokeCap(Paint.Cap.ROUND);
		mPaint.setStrokeWidth(12);
		mMyView = new MyView(this);
		mMainLayout.addView(mMyView);
		
		File f = new File("/mnt/sdcard/EventDebugger/EventDebuggerLog.txt");
		if(f!= null && f.exists()) {
			f.delete();
		}
		
		EventMonitorThread.getSU();
	}

	private void getResolution() {
		DisplayMetrics displaymetrics = new DisplayMetrics();
		getWindowManager().getDefaultDisplay().getMetrics(displaymetrics);
		Globals.height = displaymetrics.heightPixels;
		Globals.width = displaymetrics.widthPixels;
	}

	@Override
	protected void onStart() {
		super.onStart();

	}

	public Handler mHandler = new Handler() {
		@Override
		public void handleMessage(Message msg) {

		}

	};

	private void stopMonitor() {
		if (mEventMonitorThread != null && mEventMonitorThread.isAlive()) {
			EventMonitorThread.m_bMonitorOn = false;
			mEventMonitorThread.interrupt();
			mEventMonitorThread = null;
		}
	}

	private void startMonitor() {
		EventMonitorThread.m_bMonitorOn = true;
		if (mEventMonitorThread == null) {
			mEventMonitorThread = new EventMonitorThread(mHandler, this);
			mEventMonitorThread.start();
		} else {
			mEventMonitorThread.start();
		}
		mInst.setText("Draw the star without lifting finger as shown below and press mail !!!!");
	}

	private Button mCopy;
	private Button mClear;

	private void sendEmail() {
		Intent intent = new Intent(Intent.ACTION_SEND);
		intent.setType("text/plain");
		intent.putExtra(Intent.EXTRA_EMAIL, new String[] {
				"vishalsinh.jhala@infostretch.com",
				"dinesh.prajapati@infostretch.com" });
		intent.putExtra(Intent.EXTRA_SUBJECT, "Event Logs for " + android.os.Build.MODEL);
		intent.putExtra(Intent.EXTRA_TEXT,
				"Hi Vishal/Dinesh, Please find Event Debugger Log files");
		intent.putExtra(Intent.EXTRA_STREAM, Uri.parse("file://"
				+ "/mnt/sdcard/EventDebugger/EventDebuggerLog.txt"));
		startActivity(Intent.createChooser(intent, "Send Email"));
	}

	public class MyView extends View  implements OnTouchListener{

		private static final float MINP = 0.25f;
		private static final float MAXP = 0.75f;

		private Bitmap mBitmap;
		private Canvas mCanvas;
		private Path mPath;
		private Paint mBitmapPaint;

		public MyView(Context c) {
			super(c);
			mPath = new Path();
			mBitmapPaint = new Paint(Paint.DITHER_FLAG);
			this.setOnTouchListener(this);
			//setBackground(getResources().getDrawable(R.drawable.star));
		}

		@Override
		protected void onSizeChanged(int w, int h, int oldw, int oldh) {
			super.onSizeChanged(w, h, oldw, oldh);
			mBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
			mCanvas = new Canvas(mBitmap);
		}
		
		public void clearCanvas() {
            mCanvas.drawColor(Color.WHITE);
            Bitmap tempBMP = BitmapFactory.decodeResource(getResources(),R.drawable.star);
            mCanvas.drawBitmap(tempBMP, 0, 0, mBitmapPaint);
		}

		@Override
		protected void onDraw(Canvas canvas) {
			canvas.drawColor(Color.TRANSPARENT);

			canvas.drawBitmap(mBitmap, 0, 0, mBitmapPaint);

			canvas.drawPath(mPath, mPaint);
			
		}

		private float mX, mY;
		private static final float TOUCH_TOLERANCE = 4;

		private void touch_start(float x, float y) {
			mPath.reset();
			mPath.moveTo(x, y);
			mX = x;
			mY = y;
		}

		private void touch_move(float x, float y) {
			float dx = Math.abs(x - mX);
			float dy = Math.abs(y - mY);
			if (dx >= TOUCH_TOLERANCE || dy >= TOUCH_TOLERANCE) {
				mPath.quadTo(mX, mY, (x + mX) / 2, (y + mY) / 2);
				mX = x;
				mY = y;
			}
		}

		private void touch_up() {
			mPath.lineTo(mX, mY);
			// commit the path to our offscreen
			mCanvas.drawPath(mPath, mPaint);
			// kill this so we don't double draw
			mPath.reset();
		}


		@Override
		public boolean onTouch(View v, MotionEvent event) {
			float x = event.getX();
			float y = event.getY();

			switch (event.getAction()) {
			case MotionEvent.ACTION_DOWN:
				touch_start(x, y);
				invalidate();
				break;
			case MotionEvent.ACTION_MOVE:
				touch_move(x, y);
				invalidate();
				break;
			case MotionEvent.ACTION_UP:
				touch_up();
				invalidate();
				break;
			}
			return true;
		}
	}

	private void setupViews() {
		mInst = (TextView) findViewById(R.id.inst);

		mCopy = (Button) findViewById(R.id.copy);
		mCopy.setOnClickListener(new OnClickListener() {

			@Override
			public void onClick(View v) {
				stopMonitor();
				try {
					sendEmail();
				} catch (Exception e) {
					e.printStackTrace();
				}
			}
		});

		mClear = (Button) findViewById(R.id.clear);
		mClear.setOnClickListener(new OnClickListener() {

			@Override
			public void onClick(View v) {
				if(mMyView != null) {
					mMyView.clearCanvas();
					File f = new File(Environment.getExternalStorageDirectory() + "EventDebugger/EventDebuggerLog.txt");
					if(f!= null && f.exists()) {
						f.delete();
					}
				}
			}
		});
	}
}
