#pragma once

class TabControl: public CTabCtrl
{
public:
  TabControl();
  virtual ~TabControl();
  UINT GetLastMovementSource()	{return m_nSrcTab;}
  UINT GetLastMovementDestionation()	{return m_nDstTab;}
  BOOL ReorderTab( unsigned int nSrcTab, unsigned int nDstTab );

  // Overriden CTabCtrl/CWnd members
//  virtual LRESULT WindowProc( UINT message, WPARAM wParam, LPARAM lParam );

  // Command/Notification Handlers
  afx_msg void OnLButtonDown( UINT nFlags, CPoint point );
  afx_msg void OnLButtonUp( UINT nFlags, CPoint point );
  afx_msg void OnMouseMove( UINT nFlags, CPoint point );
  afx_msg void OnCaptureChanged( CWnd* );
//  afx_msg void OnPaint();
  
private:
  bool  m_bDragging;     // Specifies that whether drag 'n drop is in progress.
  UINT  m_nSrcTab;       // Specifies the source tab that is going to be moved.
  UINT  m_nDstTab;       // Specifies the destination tab (drop position).
  bool  m_bHotTracking;  // Specifies the state of whether the tab control has hot tracking enabled.
  BOOL  DragDetectPlus(CWnd* Handle, CPoint p);

  CRect m_InsertPosRect;
  CPoint m_lclickPoint;

  CSpinButtonCtrl * m_pSpinCtrl;
  
  // Utility members
  bool DrawIndicator( CPoint point );
  
  DECLARE_MESSAGE_MAP()
};
