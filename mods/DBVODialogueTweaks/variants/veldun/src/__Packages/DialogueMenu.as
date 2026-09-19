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
   var onEnterFrame;
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
   static var HIDE_NAME = "false";
   static var HIDE_EXIT = "false";
   static var HIDE_NUMBERS = "false";
   static var HIDE_TOPICS = "false";
   static var RIGHT_SIDED = "false";
   static var TOPICS_X = 0;
   static var TOPICS_Y = 0;
   static var NAME_X = 0;
   static var NAME_Y = 0;
   static var SUBS_X = 0;
   static var SUBS_Y = 0;
   static var SUBS_FONT_SIZE = 22;
   static var TOPICS_FONT_SIZE = 24;
   static var NAME_FONT_SIZE = 18;
   static var COLOR_NAME = "0xAAAAAA";
   static var COLOR_SUBTITLES = "0xFFFFFF";
   static var COLOR_NEW_SELECTED = "0xFFFFFF";
   static var COLOR_NEW_UNSELECTED = "0xFFFFFF";
   static var COLOR_OLD_SELECTED = "0x606060";
   static var COLOR_OLD_UNSELECTED = "0x606060";
   static var UNSELECTED_ALPHA = 60;
   var timerBool = false;
   function DialogueMenu()
   {
      super();
      this.TopicList = this.TopicListHolder.List_mc;
      this.eMenuState = DialogueMenu.SHOW_GREETING;
      this.bFadedIn = true;
      this.bAllowProgress = false;
      var _loc3_ = new LoadVars();
      _loc3_.load("deardiary_dm/config.txt");
      _loc3_.onData = function(str)
      {
         DialogueMenu.HIDE_NAME = DialogueMenu.ParseConfig(str,"bHideName");
         DialogueMenu.HIDE_EXIT = DialogueMenu.ParseConfig(str,"bHideExitButton");
         DialogueMenu.HIDE_NUMBERS = DialogueMenu.ParseConfig(str,"bHideNumbers");
         DialogueMenu.HIDE_TOPICS = DialogueMenu.ParseConfig(str,"bHideTopics");
         DialogueMenu.RIGHT_SIDED = DialogueMenu.ParseConfig(str,"bRightSidedList");
         DialogueMenu.TOPICS_X = parseFloat(DialogueMenu.ParseConfig(str,"fTopicsXOffset"));
         DialogueMenu.TOPICS_Y = parseFloat(DialogueMenu.ParseConfig(str,"fTopicsYOffset"));
         DialogueMenu.NAME_X = parseFloat(DialogueMenu.ParseConfig(str,"fNameXOffset"));
         DialogueMenu.NAME_Y = parseFloat(DialogueMenu.ParseConfig(str,"fNameYOffset"));
         DialogueMenu.SUBS_X = parseFloat(DialogueMenu.ParseConfig(str,"fSubtitlesXOffset"));
         DialogueMenu.SUBS_Y = parseFloat(DialogueMenu.ParseConfig(str,"fSubtitlesYOffset"));
         DialogueMenu.SUBS_FONT_SIZE = parseFloat(DialogueMenu.ParseConfig(str,"iSubtitlesFontSize"));
         DialogueMenu.TOPICS_FONT_SIZE = parseFloat(DialogueMenu.ParseConfig(str,"iTopicsFontSize"));
         DialogueMenu.NAME_FONT_SIZE = parseFloat(DialogueMenu.ParseConfig(str,"iNameFontSize"));
         DialogueMenu.COLOR_NAME = DialogueMenu.ParseConfig(str,"sNameColor");
         DialogueMenu.COLOR_SUBTITLES = DialogueMenu.ParseConfig(str,"sSubtitlesColor");
         DialogueMenu.COLOR_NEW_SELECTED = DialogueMenu.ParseConfig(str,"sNewTopicColorSel");
         DialogueMenu.COLOR_OLD_SELECTED = DialogueMenu.ParseConfig(str,"sOldTopicColorSel");
         DialogueMenu.COLOR_NEW_UNSELECTED = DialogueMenu.ParseConfig(str,"sNewTopicColorUnsel");
         DialogueMenu.COLOR_OLD_UNSELECTED = DialogueMenu.ParseConfig(str,"sOldTopicColorUnsel");
         DialogueMenu.UNSELECTED_ALPHA = parseFloat(DialogueMenu.ParseConfig(str,"fUnselTopicAlpha"));
      };
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
      if(DialogueMenu.RIGHT_SIDED == "true")
      {
         this.TopicListHolder.Lock("R");
         this.TopicListHolder._x = this.TopicListHolder._x - 400 + DialogueMenu.TOPICS_X;
         this.TopicListHolder._y -= DialogueMenu.TOPICS_Y;
      }
      else
      {
         this.TopicListHolder.Lock("L");
         this.TopicListHolder._x += DialogueMenu.TOPICS_X;
         this.TopicListHolder._y -= DialogueMenu.TOPICS_Y;
      }
      this.ExitButton.Lock("BR");
      this.ExitButton._x -= 50;
      this.ExitButton._y -= 30;
      this.ExitButton.addEventListener("click",this,"onCancelPress");
      this.TopicListHolder._visible = false;
      this.TopicListHolder.TextCopy_mc._visible = false;
      this.TopicListHolder.TextCopy_mc.textField.textColor = DialogueMenu.COLOR_OLD_SELECTED;
      this.TopicListHolder.TextCopy_mc.textField.verticalAutoSize = "top";
      this.TopicListHolder.PanelCopy_mc._visible = false;
      gfx.managers.FocusHandler.instance.setFocus(this.TopicList,0);
      this.SubtitleText.verticalAutoSize = "top";
      this.SubtitleText.SetText(" ");
      this.SpeakerName.verticalAutoSize = "top";
      this.SpeakerName.SetText(" ");
      if(DialogueMenu.HIDE_NAME == "true")
      {
         this.SpeakerName._visible = false;
      }
      this.SpeakerName._x += DialogueMenu.NAME_X;
      this.SpeakerName._y -= DialogueMenu.NAME_Y;
      this.SubtitleText._x += DialogueMenu.SUBS_X;
      this.SubtitleText._y -= DialogueMenu.SUBS_Y;
   }
   static function trim(str)
   {
      var _loc2_ = 0;
      var _loc3_ = str.length - 1;
      while(str.charCodeAt(_loc2_) < 33)
      {
         _loc2_ += 1;
      }
      while(str.charCodeAt(_loc3_) < 33)
      {
         _loc3_ -= 1;
      }
      return str.substring(_loc2_,_loc3_ + 1);
   }
   static function ParseConfig(str, par)
   {
      var _loc3_ = str.split("\n");
      var _loc4_ = 0;
      var _loc5_ = 0;
      var _loc6_;
      var _loc7_;
      var _loc8_;
      var _loc9_;
      while(_loc4_ < _loc3_.length)
      {
         if(_loc3_[_loc4_].charAt(0) != "#" && _loc3_[_loc4_].charAt(0) != "[")
         {
            _loc6_ = DialogueMenu.trim(_loc3_[_loc4_]);
            _loc7_ = _loc6_.indexOf("=");
            _loc8_ = _loc6_.substring(0,_loc7_);
            _loc9_ = DialogueMenu.trim(_loc8_);
            if(_loc9_ == par)
            {
               _loc5_ = _loc4_;
               break;
            }
         }
         _loc4_ += 1;
      }
      var _loc10_ = DialogueMenu.trim(_loc3_[_loc5_]);
      var _loc11_ = _loc10_.indexOf("=");
      var _loc12_ = _loc10_.substring(_loc11_ + 1,_loc10_.length);
      return DialogueMenu.trim(_loc12_);
   }
   function topicsFadeIn()
   {
      this.TopicListHolder._alpha = 0;
      this.onEnterFrame = function()
      {
         if(this.TopicListHolder._alpha >= 100)
         {
            delete this.onEnterFrame;
         }
         else
         {
            this.TopicListHolder._alpha += 5;
         }
      };
   }
   function topicsFadeOut()
   {
      this.TopicListHolder._alpha = 100;
      this.onEnterFrame = function()
      {
         if(this.TopicListHolder._alpha <= 0)
         {
            delete this.onEnterFrame;
         }
         else
         {
            this.TopicListHolder._alpha -= 5;
         }
      };
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
      var _loc3_ = "#" + DialogueMenu.COLOR_NAME.slice(2);
      this.SpeakerName.SetText("<font size=\'" + DialogueMenu.NAME_FONT_SIZE + "\' color=\'" + _loc3_ + "\'>" + strName + "</font>",true);
   }
   function handleInput(details, pathToFocus)
   {
      var _loc4_;
      var _loc5_;
      if(this.bFadedIn && Shared.GlobalFunc.IsKeyPressed(details))
      {
         _loc4_ = details.code;
         if(_loc4_ >= 49 and _loc4_ <= 57)
         {
            _loc5_ = _loc4_ - 49;
            if(_loc5_ < this.TopicList.EntriesA.length)
            {
               this.TopicList.selectedIndex = _loc5_;
               this.onItemSelect();
            }
         }
         else if(_loc4_ >= 97 and _loc4_ <= 105)
         {
            _loc5_ = _loc4_ - 97;
            if(_loc5_ < this.TopicList.EntriesA.length)
            {
               this.TopicList.selectedIndex = _loc5_;
               this.onItemSelect();
            }
         }
         else if(details.navEquivalent == gfx.ui.NavigationCode.TAB)
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
      var _loc3_ = "#" + DialogueMenu.COLOR_SUBTITLES.slice(2);
      this.SubtitleText.SetText("<font size=\'" + DialogueMenu.SUBS_FONT_SIZE + "\' color=\'" + _loc3_ + "\'>" + astrText + "</font>",true);
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
         if(DialogueMenu.HIDE_EXIT == "true")
         {
            this.ExitButton._visible = false;
         }
      }
   }
   function ShowDialogueList(abSlideAnim, abCopyVisible)
   {
      this.TopicListHolder._visible = true;
      this.topicsFadeIn();
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
      DialogueMenu.iMouseDownExecutionCount += 1;
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
      this.TopicListHolder.TextCopy_mc.textField.SetText("<font size=\'" + DialogueMenu.TOPICS_FONT_SIZE + "\'>" + this.TopicListHolder.List_mc.selectedEntry.text + "</font>",true);
      this.TopicListHolder.TextCopy_mc.textField.textColor = DialogueMenu.COLOR_OLD_SELECTED;
      var _loc3_ = this.TopicListHolder.TextCopy_mc._y - this.TopicListHolder.List_mc._y - this.TopicListHolder.List_mc.Entry4._y;
      this.TopicListHolder.TextCopy_mc.textField._y = 3.75 - _loc3_;
      skse.SendModEvent("CutNpcDBVOReply","");
      this.initDBVO();
      if(DialogueMenu.HIDE_TOPICS == "true")
      {
         this.topicsFadeOut();
      }
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
         // Backstop only: deliberately long (words*300 + 2000ms). The DLL normally fires
         // dbvoOnPlayerLineEnded the instant the line ends, which clears this timer and reschedules
         // topicClicked after the short dbvoPadMs gap. This fires the reply ONLY if the DLL never
         // reports the end (DLL absent, or end undetected) — so it must comfortably outlast any line.
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
