class DialogueMenu extends MovieClip
{
   var ExitButton;
   var SpeakerName;
   var SubtitleText;
   var TopicList;
   var TopicListHolder;
   var bAllowProgress;
   var bFadedIn;
   var eMenuState;
   var iAllowProgressTimerID;
   var timer;
   var skipArmedAt;
   var dbvoPadMs;
   static var ALLOW_PROGRESS_DELAY = 750;
   static var SKIP_DEBOUNCE_MS = 250;
   static var iMouseDownExecutionCount = 0;
   static var SHOW_GREETING = 0;
   static var TOPIC_LIST_SHOWN = 1;
   static var TOPIC_CLICKED = 2;
   static var TRANSITIONING = 3;
   var timerBool = false;
   function DialogueMenu()
   {
      super();
      this.TopicList = this.TopicListHolder.List_mc;
      this.eMenuState = DialogueMenu.SHOW_GREETING;
      this.bFadedIn = true;
      this.bAllowProgress = false;
   }
   function InitExtensions()
   {
      Mouse.addListener(this);
      gfx.io.GameDelegate.addCallBack("Cancel",this,"onCancelPress");
      gfx.io.GameDelegate.addCallBack("ShowDialogueText",this,"ShowDialogueText");
      gfx.io.GameDelegate.addCallBack("HideDialogueText",this,"HideDialogueText");
      gfx.io.GameDelegate.addCallBack("PopulateDialogueList",this,"PopulateDialogueLists");
      gfx.io.GameDelegate.addCallBack("ShowDialogueList",this,"DoShowDialogueList");
      gfx.io.GameDelegate.addCallBack("StartHideMenu",this,"StartHideMenu");
      gfx.io.GameDelegate.addCallBack("SetSpeakerName",this,"SetSpeakerName");
      gfx.io.GameDelegate.addCallBack("NotifyVoiceReady",this,"OnVoiceReady");
      gfx.io.GameDelegate.addCallBack("AdjustForPALSD",this,"AdjustForPALSD");
      this.TopicList.addEventListener("listMovedUp",this,"playListUpAnim");
      this.TopicList.addEventListener("listMovedDown",this,"playListDownAnim");
      this.TopicList.addEventListener("itemPress",this,"onItemSelect");
      Shared.GlobalFunc.SetLockFunction();
      this.ExitButton.Lock("BR");
      this.ExitButton._x -= 50;
      this.ExitButton._y -= 30;
      this.ExitButton.addEventListener("click",this,"onCancelPress");
      this.TopicListHolder._visible = false;
      this.TopicListHolder.TextCopy_mc._visible = false;
      this.TopicListHolder.TextCopy_mc.textField.textColor = 6316128;
      this.TopicListHolder.TextCopy_mc.textField.verticalAutoSize = "top";
      this.TopicListHolder.PanelCopy_mc._visible = false;
      gfx.managers.FocusHandler.instance.setFocus(this.TopicList,0);
      this.SubtitleText.verticalAutoSize = "top";
      this.SubtitleText.SetText(" ");
      this.SpeakerName.verticalAutoSize = "top";
      this.SpeakerName.SetText(" ");
   }
   function AdjustForPALSD()
   {
      _root.DialogueMenu_mc._x -= 35;
   }
   function SetPlatform(aiPlatform, abPS3Switch)
   {
      this.ExitButton.SetPlatform(aiPlatform,abPS3Switch);
      this.TopicList.SetPlatform(aiPlatform,abPS3Switch);
   }
   function SetSpeakerName(strName)
   {
      this.SpeakerName.SetText(strName);
   }
   function handleInput(details, pathToFocus)
   {
      if(this.bFadedIn && Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.TAB)
         {
            this.onCancelPress();
         }
         else if(details.navEquivalent != gfx.ui.NavigationCode.UP && details.navEquivalent != gfx.ui.NavigationCode.DOWN || this.eMenuState == DialogueMenu.TOPIC_LIST_SHOWN)
         {
            pathToFocus[0].handleInput(details,pathToFocus.slice(1));
         }
      }
      return true;
   }
   function get menuState()
   {
      return this.eMenuState;
   }
   function set menuState(aNewState)
   {
      this.eMenuState = aNewState;
   }
   function ShowDialogueText(astrText)
   {
      this.SubtitleText.SetText(astrText);
   }
   function OnVoiceReady()
   {
      this.StartProgressTimer();
   }
   function StartProgressTimer()
   {
      this.bAllowProgress = false;
      clearInterval(this.iAllowProgressTimerID);
      this.iAllowProgressTimerID = setInterval(this,"SetAllowProgress",DialogueMenu.ALLOW_PROGRESS_DELAY);
   }
   function HideDialogueText()
   {
      this.SubtitleText.SetText(" ");
   }
   function SetAllowProgress()
   {
      clearInterval(this.iAllowProgressTimerID);
      this.bAllowProgress = true;
   }
   function PopulateDialogueLists()
   {
      var _loc3_ = 0;
      var _loc4_ = 1;
      var _loc5_ = 2;
      var _loc6_ = 3;
      this.TopicList.ClearList();
      var _loc7_ = 0;
      var _loc8_;
      while(_loc7_ < arguments.length - 1)
      {
         _loc8_ = {text:arguments[_loc7_ + _loc3_],topicIsNew:arguments[_loc7_ + _loc4_],topicIndex:arguments[_loc7_ + _loc5_]};
         this.TopicList.entryList.push(_loc8_);
         _loc7_ += _loc6_;
      }
      if(arguments[arguments.length - 1] != -1)
      {
         this.TopicList.SetSelectedTopic(arguments[arguments.length - 1]);
      }
      this.TopicList.InvalidateData();
   }
   function DoShowDialogueList(abNewList, abHideExitButton)
   {
      if(this.timerBool == false)
      {
         if(this.eMenuState == DialogueMenu.TOPIC_CLICKED || this.eMenuState == DialogueMenu.SHOW_GREETING && this.TopicList.entryList.length > 0)
         {
            this.ShowDialogueList(abNewList,abNewList && this.eMenuState == DialogueMenu.TOPIC_CLICKED);
         }
         this.ExitButton._visible = !abHideExitButton;
      }
   }
   function ShowDialogueList(abSlideAnim, abCopyVisible)
   {
      this.TopicListHolder._visible = true;
      this.TopicListHolder.gotoAndPlay(!abSlideAnim ? "fadeListIn" : "slideListIn");
      this.eMenuState = DialogueMenu.TRANSITIONING;
      this.TopicListHolder.TextCopy_mc._visible = abCopyVisible;
      this.TopicListHolder.PanelCopy_mc._visible = abCopyVisible;
   }
   function onItemSelect(event)
   {
      if(this.eMenuState == DialogueMenu.TOPIC_CLICKED && this.timerBool && this.timer != undefined)
      {
         this.trySkipPlayerLine();
         return undefined;
      }
      if(this.bAllowProgress && event.keyboardOrMouse != 0)
      {
         if(this.eMenuState == DialogueMenu.TOPIC_LIST_SHOWN)
         {
            this.onSelectionClick(event && event.mouseClick);
         }
         else if(this.eMenuState == DialogueMenu.TOPIC_CLICKED || this.eMenuState == DialogueMenu.SHOW_GREETING)
         {
            this.SkipText();
         }
         this.bAllowProgress = false;
      }
   }
   function SkipText()
   {
      if(this.bAllowProgress)
      {
         gfx.io.GameDelegate.call("SkipText",[]);
         this.bAllowProgress = false;
      }
   }
   function onMouseDown()
   {
      DialogueMenu.iMouseDownExecutionCount++;
      if(DialogueMenu.iMouseDownExecutionCount % 2 != 0)
      {
         this.onItemSelect({mouseClick:true});
      }
   }
   function onCancelPress()
   {
      if(this.eMenuState == DialogueMenu.SHOW_GREETING)
      {
         this.SkipText();
         return undefined;
      }
      this.StartHideMenu();
   }
   function StartHideMenu()
   {
      this.SubtitleText._visible = false;
      this.bFadedIn = false;
      this.SpeakerName.SetText(" ");
      this.ExitButton._visible = false;
      this._parent.gotoAndPlay("startFadeOut");
      gfx.io.GameDelegate.call("CloseMenu",[]);
   }
   function playListUpAnim(aEvent)
   {
      if(aEvent.scrollChanged == true)
      {
         aEvent.target._parent.gotoAndPlay("moveUp");
      }
   }
   function playListDownAnim(aEvent)
   {
      if(aEvent.scrollChanged == true)
      {
         aEvent.target._parent.gotoAndPlay("moveDown");
      }
   }
   function onSelectionClick(abMouseClick)
   {
      this.timerBool = true;
      if(abMouseClick)
      {
         this.TopicList.SetSelectedIndexByMouse(false);
      }
      if(this.eMenuState == DialogueMenu.TOPIC_LIST_SHOWN)
      {
         this.eMenuState = DialogueMenu.TOPIC_CLICKED;
      }
      if(this.TopicList.scrollPosition != this.TopicList.selectedIndex)
      {
         this.TopicList.RestoreScrollPosition(this.TopicList.selectedIndex,true);
         this.TopicList.UpdateList();
      }
      this.TopicListHolder.gotoAndPlay("topicClicked");
      this.TopicListHolder.TextCopy_mc._visible = true;
      this.TopicListHolder.TextCopy_mc.textField.SetText(this.TopicListHolder.List_mc.selectedEntry.text);
      var _loc3_ = this.TopicListHolder.TextCopy_mc._y - this.TopicListHolder.List_mc._y - this.TopicListHolder.List_mc.Entry4._y;
      this.TopicListHolder.TextCopy_mc.textField._y = 6.25 - _loc3_;
      skse.SendModEvent("CutNpcDBVOReply","");
      this.initDBVO();
   }
   function initDBVO()
   {
      var _loc2_ = this.TopicListHolder.List_mc.selectedEntry.text;
      var _loc3_ = _loc2_.split(" (")[0].split(" ").join("_").split("/").join("_").split("\\").join("_").split(":").join("_").split("*").join("_").split("?").join("_").split("\"").join("_").split("<").join("_").split(">").join("_").split("|").join("_");
      skse.SendModEvent("PlayDBVOTopic",_loc3_);
   }
   function startTopicClickedTimer(voicePackID)
   {
      var _loc3_;
      var _loc4_;
      if(voicePackID == "off")
      {
         this.timerBool = false;
         gfx.io.GameDelegate.call("TopicClicked",[this.TopicList.selectedEntry.topicIndex]);
      }
      else
      {
         _loc3_ = this.TopicListHolder.List_mc.selectedEntry.text;
         _loc4_ = Math.round(_loc3_.split(" (")[0].split(" ").length * 300) + 2000;
         this.timer = setTimeout(this,"topicClicked",_loc4_);
         this.skipArmedAt = getTimer();
      }
   }
   function dbvoOnPlayerLineEnded()
   {
      if(this.eMenuState == DialogueMenu.TOPIC_CLICKED && this.timerBool && this.timer != undefined)
      {
         clearTimeout(this.timer);
         var gap = this.dbvoPadMs >= 0 ? this.dbvoPadMs : 250;
         this.timer = setTimeout(this,"topicClicked",gap);
      }
   }
   function topicClicked()
   {
      this.timerBool = false;
      gfx.io.GameDelegate.call("TopicClicked",[this.TopicList.selectedEntry.topicIndex]);
   }
   function trySkipPlayerLine()
   {
      if(this.eMenuState == DialogueMenu.TOPIC_CLICKED && this.timerBool && this.timer != undefined && getTimer() - this.skipArmedAt >= DialogueMenu.SKIP_DEBOUNCE_MS)
      {
         skse.SendModEvent("CutPlayerDBVOLine","");
         clearTimeout(this.timer);
         this.timer = undefined;
         this.topicClicked();
      }
   }
   function onFadeOutCompletion()
   {
      gfx.io.GameDelegate.call("FadeDone",[]);
   }
}
