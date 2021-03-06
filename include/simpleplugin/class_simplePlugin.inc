<?php
/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2012-2016  FusionDirectory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*! \brief This class is made for easy plugin creation for editing LDAP attributes
 *
 */
class simplePlugin extends plugin
{
  /*! \brief This attribute store all information about attributes
   *
   */
  var $attributesInfo;
  /*! \brief This attribute store references toward attributes
   *
   * associative array that stores attributeLdapName => reference on object
   */
  var $attributesAccess = array();

  /*! \brief Do we want a header allowing to able/disable this plugin
   *
   */
  protected $displayHeader = FALSE;

  /*! \brief Is this plugin the main tab, the one that handle the object itseld
   *
   */
  protected $mainTab = FALSE;

  protected $header = "";

  public $orig_dn = "";

  protected $templatePath;

  /*! \brief Are we executed in a edit-mode environment? (this is FALSE if we're called from management, TRUE if we're called from a main.inc)
   */
  protected $needEditMode = FALSE;

  /*! \brief ldap error cache
   *
   */
  protected $ldap_error;

  /*! \brief Attributes that needs to be initialized before the others.
   *
   */
  protected $preInitAttributes = array();

  /*! \brief FALSE to disable inheritance. Array like array ('objectClass' => 'attribute') to specify oc of the groups it might be inherited from
   *
   */
  protected $inheritance      = FALSE;
  protected $member_of_group  = FALSE;
  protected $editing_group    = NULL;
  protected $group_attrs      = array();

  /*! \brief constructor
   *
   *  \param string $dn The dn of this instance
   *  \param Object $object An object to copy values from
   *  \param array $attributesInfo An attributesInfo array, if NULL, getAttributesInfo will be used.
   *
   */
  function __construct ($dn = NULL, $object = NULL, $parent = NULL, $mainTab = FALSE, $attributesInfo = NULL)
  {
    global $config;

    $this->parent   = $parent;
    $this->mainTab  = $mainTab;

    if ($attributesInfo === NULL) {
      $attributesInfo = $this->getAttributesInfo();
    }
    if (!$this->displayHeader) {
      // If we don't display the header to activate/deactive the plugin, that means it's always activated
      $this->ignore_account = TRUE;
    }

    $this->attributesInfo = array();
    foreach ($attributesInfo as $section => $sectionInfo) {
      $attrs = array();
      foreach ($sectionInfo['attrs'] as $attr) {
        $name = $attr->getLdapName();
        if (isset($attrs[$name])) {
          // We check that there is no duplicated attribute name
          trigger_error("Duplicated attribute LDAP name '$name' in a simplePlugin subclass");
        }
        // We make so that attribute have their LDAP name as key
        // That allow the plugin to use $this->attributesInfo[$sectionName]['attrs'][$myLdapName] to retreive the attribute info.
        $attrs[$name] = $attr;
      }
      $sectionInfo['attrs']           = $attrs;
      $this->attributesInfo[$section] = $sectionInfo;
      foreach ($this->attributesInfo[$section]['attrs'] as $name => $attr) {
        if (isset($this->attributesAccess[$name])) {
          // We check that there is no duplicated attribute name
          trigger_error("Duplicated attribute LDAP name '$name' in a simplePlugin subclass");
        }
        $this->attributesAccess[$name] =& $this->attributesInfo[$section]['attrs'][$name];
        unset($this->$name);
      }
    }

    // We call plugin constructor
    parent::__construct($dn, $object);

    $this->setTemplate($this->is_template);

    $this->orig_dn = $dn;

    if ($this->mainTab) {
      $this->is_account = TRUE;
      /* TODO: When merging plugin and simplePlugin we should find a way to avoid doing a separate LDAP fetch for this */
      $this->entryCSN = getEntryCSN($this->dn);
    }

    if (!isset($this->templatePath)) {
      $this->templatePath = get_template_path('simpleplugin.tpl');
    }

    if (is_array($this->inheritance)) {
      /* Check group membership */
      $ldap = $config->get_ldap_link();
      $ldap->cd($config->current['BASE']);
      foreach ($this->inheritance as $oc => $at) {
        if ($this->mainTab) {
          $filter = '(&(objectClass='.$oc.')('.$at.'='.ldap_escape_f($this->dn).'))';
        } else {
          $filter = '(&(objectClass='.$oc.')'.$this->getObjectClassFilter().'('.$at.'='.ldap_escape_f($this->dn).'))';
        }
        $ldap->search($filter, $this->attributes);
        if ($ldap->count() == 1) {
          $this->member_of_group = TRUE;
          $attrs = $ldap->fetch();
          $this->group_attrs = $attrs;
          break;
        }
      }
    }
  }

  function setTemplate ($bool)
  {
    parent::setTemplate($bool);
    if ($this->is_template && $this->mainTab) {
      /* Unshift special section for template infos */
      $this->attributesInfo = array_merge(
        array(
          '_template' => array(
            'class' => array('fullwidth'),
            'name'  => _('Template settings'),
            'attrs' => array(
              '_template_cn' => new StringAttribute(
                _('Template name'), _('This is the name of the template'),
                '_template_cn', TRUE,
                '', 'template_cn'
              )
            )
          ),
          '_template_dummy' => array(
            'class' => array('invisible'),
            'name'  => '_template_dummy',
            'attrs' => array()
          )
        ),
        $this->attributesInfo
      );
      $this->attributesAccess['_template_cn'] =& $this->attributesInfo['_template']['attrs']['_template_cn'];
      $this->attributesAccess['_template_cn']->setInLdap(FALSE);
      $this->attributesAccess['_template_cn']->setValue($this->_template_cn);
      $this->attributesAccess['_template_cn']->setParent($this);
      unset($this->_template_cn);
    }
  }

  protected function loadAttributes()
  {
    // We load attributes values
    // First the one flagged as preInit
    foreach ($this->preInitAttributes as $attr) {
      $this->attributesAccess[$attr]->setParent($this);
      $this->attributesAccess[$attr]->loadValue($this->attrs);
    }
    // Then the others
    foreach ($this->attributesInfo as &$sectionInfo) {
      foreach ($sectionInfo['attrs'] as $name => &$attr) {
        if (in_array($name, $this->preInitAttributes)) {
          /* skip the preInit ones */
          continue;
        }
        $attr->setParent($this);
        $attr->loadValue($this->attrs);
      }
      unset($attr);
    }
    unset($sectionInfo);

    $this->updateAttributesValues();
  }

  /*! \brief This function allows to use the syntax $plugin->attributeName to get attributes values
   * It calls the getValue method on the concerned attribute
   * It also adds the $plugin->attribtues syntax to get attributes list
   */
  public function __get($name)
  {
    if ($name == 'attributes') {
      $plugin = $this;
      return array_filter(array_keys($this->attributesAccess),
        function ($a) use ($plugin)
        {
          return $plugin->attributesAccess[$a]->isInLdap();
        }
      );
    } elseif (isset($this->attributesAccess[$name])) {
      return $this->attributesAccess[$name]->getValue();
    } else {
      /* Calling default behaviour */
      return $this->$name;
    }
  }

  /*! \brief This function allows to use the syntax $plugin->attributeName to set attributes values

    It calls the setValue method on the concerned attribute
   */
  public function __set($name, $value)
  {
    if (isset($this->attributesAccess[$name])) {
      $this->attributesAccess[$name]->setValue($value);
    } else {
      /* Calling default behaviour */
      $this->$name = $value;
    }
  }

  /*! \brief This function allows to use the syntax isset($plugin->attributeName)

    It returns FALSE if the attribute has an empty value.
   */
  public function __isset($name)
  {
    if ($name == 'attributes') {
      return TRUE;
    }
    return isset($this->attributesAccess[$name]);
  }

  /*! \brief This function returns the dn this object should have
   */
  public function compute_dn()
  {
    global $config;
    if (!$this->mainTab) {
      msg_dialog::display(_('Fatal error'), _('Only main tab can compute dn'), FATAL_ERROR_DIALOG);
      exit;
    }
    if (!isset($this->parent) || !($this->parent instanceof simpleTabs)) {
      msg_dialog::display(
        _('Fatal error'),
        sprintf(
          _('Could not compute dn: no parent tab class for "%s"'),
          get_class($this)
        ),
        FATAL_ERROR_DIALOG
      );
      exit;
    }
    $infos = $this->parent->objectInfos();
    if ($infos === FALSE) {
      msg_dialog::display(
        _('Fatal error'),
        sprintf(
          _('Could not compute dn: could not find objectType infos from tab class "%s"'),
          get_class($this->parent)
        ),
        FATAL_ERROR_DIALOG
      );
      exit;
    }
    $attr = $infos['mainAttr'];
    $ou   = $infos['ou'];
    if (isset($this->base)) {
      $base = $this->base;
    } else {
      $base = $config->current['BASE'];
    }
    if ($this->is_template) {
      $dn = 'cn='.ldap_escape_dn($this->_template_cn).',ou=templates,'.$ou.$base;
      return $dn;
    }
    return $attr.'='.ldap_escape_dn($this->attributesAccess[$attr]->computeLdapValue()).','.$ou.$base;
  }

  function getRequiredAttributes()
  {
    $tmp = array();
    foreach ($this->attributesAccess as $attr) {
      if ($attr->isRequired()) {
        $tmp[] = $attr->getLdapName();
      }
    }
    return $tmp;
  }

  /*! \brief This function update the values of LDAP attributes as class attribute,
    so that plugin code works correctly

    This should be removed once plugin class is not used anymore
   */
  function updateAttributesValues ()
  {
    unset($this->attributes);
    /* This will call __get with attributes, which will create the array */
    $this->attributes = $this->attributes;
  }

  function editing_group ()
  {
    if ($this->editing_group == NULL) {
      if (isset($this->parent)) {
        $this->editing_group = (get_class($this->parent->getBaseObject()) == 'ogroup');
      } else {
        return NULL;
      }
    }
    return $this->editing_group;
  }

  /*! \brief This function display the plugin and return the html code
   */
  function execute ()
  {
    @DEBUG (DEBUG_TRACE, __LINE__, __FUNCTION__, __FILE__, $this->dn, "execute");
    parent::execute();

    $this->displayPlugin  = TRUE;
    $this->header         = "";

    if (is_object($this->dialog)) {
      $dialogResult = $this->dialog->execute();
      if ($dialogResult === FALSE) {
        $this->closeDialog();
      } else {
        $this->header         = $dialogResult;
        $this->displayPlugin  = FALSE;
        return $this->header;
      }
    }

    if ($this->displayHeader) {
      /* Show tab dialog headers */
      $plInfo = pluglist::pluginInfos(get_class($this));
      if ($this->parent !== NULL) {
        if ($this->is_account) {
          $disabled = $this->acl_skip_write();
          $depends = array();
          if (isset($plInfo['plDepending'])) {
            foreach ($plInfo['plDepending'] as $plugin) {
              if (isset($this->parent->by_object[$plugin]) &&
                  $this->parent->by_object[$plugin]->is_account) {
                $disabled       = TRUE;
                $dependPlInfos  = pluglist::pluginInfos($plugin);
                $depends[]      = $dependPlInfos['plShortName'];
              }
            }
          }
          $this->header = $this->show_disable_header(
            msgPool::removeFeaturesButton($plInfo['plShortName']),
            msgPool::featuresEnabled($plInfo['plShortName'], $depends),
            $disabled,
            get_class($this).'_modify_state'
          );
        } else {
          $disabled = $this->acl_skip_write();
          $depends = array();
          if (isset($plInfo['plDepends'])) {
            foreach ($plInfo['plDepends'] as $plugin) {
              if (!isset($this->parent->by_object[$plugin]) ||
                  !$this->parent->by_object[$plugin]->is_account) {
                $disabled   = TRUE;
                $dependPlInfos  = pluglist::pluginInfos($plugin);
                $depends[]      = $dependPlInfos['plShortName'];
              }
            }
          }
          $this->header = $this->show_enable_header(
            msgPool::addFeaturesButton($plInfo['plShortName']),
            msgPool::featuresDisabled($plInfo['plShortName'], $depends),
            $disabled,
            get_class($this).'_modify_state'
          );
          $this->displayPlugin = FALSE;
          return $this->header.$this->inheritanceDisplay();
        }
      } elseif (!$this->is_account) {
        $this->header = '<img alt="" src="geticon.php?context=status&amp;icon=dialog-error&amp;size=16" align="middle"/>&nbsp;<b>'.
                        msgPool::noValidExtension($plInfo['plShortName'])."</b>";
        $this->header .= back_to_main();
        $this->displayPlugin = FALSE;
        return $this->header.$this->inheritanceDisplay();
      }
    }

    $smarty = get_smarty();

    $this->renderAttributes(FALSE);
    $smarty->assign("hiddenPostedInput", get_class($this)."_posted");
    if (isset($this->focusedField)) {
      $smarty->assign("focusedField", $this->focusedField);
      unset($this->focusedField);
    } else {
      $smarty->assign("focusedField", key($this->attributesAccess));
    }

    return $this->header.$smarty->fetch($this->templatePath);
  }

  /*! \brief Check if logged in user have enough right to write this attribute value
   *
   * \param mixed $attr Attribute object or name (in this case it will be fetched from attributesAccess)
   */
  function attrIsWriteable($attr)
  {
    if (!is_object($attr)) {
      $attr = $this->attributesAccess[$attr];
    }
    if ($attr->getLdapName() == 'base') {
      if (!$this->acl_skip_write() && (!$this->initially_was_account || $this->acl_is_moveable() || $this->acl_is_removeable())) {
        return TRUE;
      } else {
        return FALSE;
      }
    }
    return $this->acl_is_writeable($attr->getAcl(), $this->acl_skip_write());
  }

  function renderAttributes($readOnly = FALSE)
  {
    global $ui;
    $smarty = get_smarty();

    if ($this->is_template) {
      $smarty->assign('template_cnACL', $ui->get_permissions($this->acl_base, $this->acl_category.'template', 'template_cn', $this->acl_skip_write()));
    }

    /* Handle rights to modify the base */
    if (isset($this->attributesAccess['base'])) {
      if ($this->attrIsWriteable('base')) {
        $smarty->assign('baseACL', 'rw');
      } else {
        $smarty->assign('baseACL', 'r');
      }
    }

    $sections = array();
    foreach ($this->attributesInfo as $section => $sectionInfo) {
      $legend = $sectionInfo['name'];
      if (isset($sectionInfo['icon'])) {
        $legend = '<img '.
                  'src="'.htmlentities($sectionInfo['icon'], ENT_COMPAT, 'UTF-8').'" '.
                  'alt="section '.$sectionInfo['name'].' icon" '.
                  'title="section '.$sectionInfo['name'].' icon" '.
                  '/>'.$legend;
      }
      $smarty->assign("section", $legend);
      $smarty->assign("sectionId", $section);
      if (isset($sectionInfo['class'])) {
        $smarty->assign("sectionClasses", ' '.join(' ', $sectionInfo['class']));
      } else {
        $smarty->assign("sectionClasses", '');
      }
      $attributes = array();
      foreach ($sectionInfo['attrs'] as $attr) {
        if ($attr->getAclInfo() !== FALSE) {
          // We assign ACLs so that attributes can use them in their template code
          $smarty->assign($attr->getAcl()."ACL", $this->getacl($attr->getAcl(), $this->acl_skip_write()));
        }
        $attr->renderAttribute($attributes, $readOnly);
      }
      $smarty->assign("attributes", $attributes);
      // We fetch each section with the section template
      if (isset($sectionInfo['template'])) {
        $displaySection = $smarty->fetch($sectionInfo['template']);
      } else {
        $displaySection = $smarty->fetch(get_template_path('simpleplugin_section.tpl'));
      }
      $sections[$section] = $displaySection;
    }
    $smarty->assign("sections", $sections);
  }

  function inheritanceDisplay()
  {
    if (!$this->member_of_group) {
      return "";
    }
    $class = get_class($this);
    $attrsWrapper = new stdClass();
    $attrsWrapper->attrs = $this->group_attrs;
    $group = new $class($this->group_attrs['dn'], $attrsWrapper, $this->parent, $this->mainTab);
    $smarty = get_smarty();

    $group->renderAttributes(TRUE);
    $smarty->assign("hiddenPostedInput", get_class($this)."_posted");

    return "<h1>Inherited information:</h1><div></div>\n".$smarty->fetch($this->templatePath);
  }

  /*! \brief This function allows you to open a dialog
   *
   *  \param mixed $dialog The dialog object
   */
  function openDialog ($dialog)
  {
    $this->dialog = $dialog;
  }

  /*! \brief This function closes the dialog
   */
  function closeDialog ()
  {
    $this->dialog = NULL;
  }

  public function setNeedEditMode ($bool)
  {
    $this->needEditMode = $bool;
  }

  protected function acl_skip_write ()
  {
    return ($this->needEditMode && !session::is_set('edit'));
  }

  /*! \brief This function removes the object from LDAP
   */
  function remove_from_parent()
  {
    if (!$this->initially_was_account || !$this->acl_is_removeable()) {
      return;
    }
    $this->prepare_remove();
    if ($this->is_template && (!defined('_OLD_TEMPLATES_') || !_OLD_TEMPLATES_)) {
      $this->attrs = $this->templateSaveAttrs();
      $this->saved_attributes = array();
    }
    if ($this->ldap_remove()) {
      $this->post_remove();
    }
  }

  protected function prepare_remove ()
  {
    /* Remove FusionDirectory attributes */
    plugin::remove_from_parent();
  }

  protected function ldap_remove ()
  {
    global $config;
    $ldap = $config->get_ldap_link();
    if ($this->mainTab) {
      $ldap->rmdir_recursive($this->dn);
    } else {
      $this->cleanup();
      $ldap->cd($this->dn);
      $ldap->modify($this->attrs);
    }

    $this->ldap_error = $ldap->get_error();

    if ($ldap->success()) {
      return TRUE;
    } else {
      msg_dialog::display(_("LDAP error"), msgPool::ldaperror($ldap->get_error(), $this->dn, LDAP_MOD, get_class()), LDAP_ERROR);
      return FALSE;
    }
  }

  protected function post_remove ()
  {
    logging::log('remove', 'plugin/'.get_class($this), $this->dn, array_keys($this->attrs), $this->ldap_error);

    /* Optionally execute a command after we're done */
    $this->handle_post_events('remove');
  }

  /*! \brief This function handle $_POST informations
   */
  function save_object ()
  {
    @DEBUG (DEBUG_TRACE, __LINE__, __FUNCTION__, __FILE__, $this->dn, "save_object");
    if ($this->displayHeader && isset($_POST[get_class($this).'_modify_state'])) {
      if ($this->is_account && $this->acl_is_removeable()) {
        $this->is_account = FALSE;
      } elseif (!$this->is_account && $this->acl_is_createable()) {
        $this->is_account = TRUE;
      }
    }
    if (isset($_POST[get_class($this)."_posted"])) {
      // If our form has been posted
      // A first pass that loads the post values
      foreach ($this->attributesInfo as $section => &$sectionInfo) {
        foreach ($sectionInfo['attrs'] as &$attr) {
          if ($this->attrIsWriteable($attr)) {
            // Each attribute know how to read its value from POST
            $attr->loadPostValue();
          }
        }
        unset ($attrs);
      }
      unset($sectionInfo);
      // A second one that applies them. That allow complex stuff such as attribute disabling
      foreach ($this->attributesInfo as $section => &$sectionInfo) {
        foreach ($sectionInfo['attrs'] as &$attr) {
          if ($this->attrIsWriteable($attr)) {
            // Each attribute know how to read its value from POST
            $attr->applyPostValue();
          }
        }
        unset ($attrs);
      }
      unset($sectionInfo);
      $this->updateAttributesValues();
    }
  }

  /*! \brief This function saves the object in the LDAP
   */
  function save ()
  {
    @DEBUG (DEBUG_TRACE, __LINE__, __FUNCTION__, __FILE__, $this->dn, "save");
    $this->prepare_save();
    if ($this->is_template && (!defined('_OLD_TEMPLATES_') || !_OLD_TEMPLATES_)) {
      $this->attrs = $this->templateSaveAttrs();
      $this->saved_attributes = array();
    }
    $this->ldap_save();
    $this->post_save();
  }

  protected function prepare_save ()
  {
    /* Call parents save to prepare $this->attrs */
    parent::save();

    /* Fill attributes LDAP values into the attrs array */
    foreach ($this->attributesInfo as $section => $sectionInfo) {
      foreach ($sectionInfo['attrs'] as $attr) {
        $attr->fillLdapValue($this->attrs);
      }
    }
    /* Some of them have post-filling hook */
    foreach ($this->attributesInfo as $section => $sectionInfo) {
      foreach ($sectionInfo['attrs'] as $attr) {
        $attr->fillLdapValueHook($this->attrs);
      }
    }
  }

  protected function ldap_save ($cleanup = TRUE)
  {
    global $config;
    /* Check if this is a new entry ... add/modify */
    $ldap = $config->get_ldap_link();
    $ldap->cat($this->dn, array("objectClass"));
    if ($this->mainTab && !$this->initially_was_account) {
      if ($ldap->count()) {
        msg_dialog::display(_("Error when saving"), sprintf(_('There is already an entry with the same dn : %s'), $this->dn), ERROR_DIALOG);
        return;
      }
      $ldap->cd($config->current['BASE']);
      $ldap->create_missing_trees(preg_replace('/^[^,]+,/', '', $this->dn));
      $action = "add";
    } else {
      if (!$ldap->count()) {
        msg_dialog::display(_("Error when saving"), sprintf(_('The entry %s is not existing'), $this->dn), ERROR_DIALOG);
        return;
      }
      $action = "modify";
    }

    if ($cleanup) {
      $this->cleanup();
    }
    $ldap->cd($this->dn);
    $ldap->$action($this->attrs);
    $this->ldap_error = $ldap->get_error();

    /* Check for errors */
    if (!$ldap->success()) {
      msg_dialog::display(_("LDAP error while $action"), msgPool::ldaperror($ldap->get_error(), $this->dn, 0, get_class()), LDAP_ERROR);
    }
  }

  /*! \brief This function is called after LDAP save to do some post operations and logging
   *
   * This function calls hooks, update foreign keys and log modification
   */
  protected function post_save()
  {
    /* Propagate and log the event */
    if ($this->initially_was_account) {
      $this->handle_post_events('modify');
      logging::log('modify', 'plugin/'.get_class($this), $this->dn, array_keys($this->attrs), $this->ldap_error);
    } else {
      $this->handle_post_events('add');
      logging::log('create', 'plugin/'.get_class($this), $this->dn, array_keys($this->attrs), $this->ldap_error);
    }
  }

  /*! \brief This function checks the attributes values and yell if something is wrong
   */
  function check ()
  {
    @DEBUG (DEBUG_TRACE, __LINE__, __FUNCTION__, __FILE__, $this->dn, 'check');
    $message = array();

    foreach ($this->attributesInfo as $sectionInfo) {
      foreach ($sectionInfo['attrs'] as $attr) {
        $error = $attr->check();
        if (!empty($error)) {
          if (is_array($error)) {
            $message = array_merge($message, $error);
          } else {
            $message[] = $error;
          }
        }
      }
    }

    $this->callHook('CHECK', array('nbCheckErrors' => count($message)), $returnOutput);
    if (!empty($returnOutput)) {
      $message[] = join("\n", $returnOutput);
    }

    /* Check entryCSN */
    if (!empty($this->entryCSN)) {
      $current_csn = getEntryCSN($this->dn);
      if (($current_csn != $this->entryCSN) && !empty($current_csn)) {
        $this->entryCSN = $current_csn;
        $message[] = _('The object has changed since opened in FusionDirectory. All changes that may be done by others will get lost if you save this entry!');
      }
    }

    return $message;
  }

  /*! \brief This function generate the needed ACLs for a given attribtues array
   *
   *  \param array $attributesInfo the attribute array
   */
  static function generatePlProvidedAcls ($attributesInfo)
  {
    $plProvidedAcls = array();
    foreach ($attributesInfo as $sectionInfo) {
      foreach ($sectionInfo['attrs'] as $attr) {
        $aclInfo = $attr->getAclInfo();
        if ($aclInfo !== FALSE) {
          $plProvidedAcls[$aclInfo['name']] = $aclInfo['desc'];
        }
      }
    }

    return $plProvidedAcls;
  }

  /*! \brief This function is the needed main.inc for users tab
   *
   *  \param array $classname the plugin class name
   */
  static function userMainInc ($classname)
  {
    global $ui;
    trigger_error("userMainInc is deprecated");
    return static::mainInc($classname, $ui->dn);
  }

  /*! \brief This function is the needed main.inc for plugins that are not used inside a management class
   *
   *  \param array $classname the class name to read plInfo from. (plIcon, plShortname and plCategory are gonna be used)
   *
   *  \param string $entry_dn the dn of the object to show/edit
   *
   *  \param boolean $tabs TRUE to use tabs, FALSE to show directly the plugin class
   *
   *  \param boolean $edit_mode wether or not this plugin can be edited
   *
   *  \param string $objectType The objectType to use (will be taken in the plInfo if FALSE)
   *
   */
  static function mainInc ($classname, $entry_dn, $tabs = FALSE, $edit_mode = TRUE, $objectType = FALSE)
  {
    global $remove_lock, $cleanup, $display, $config, $plug, $ui;

    $plInfo     = pluglist::pluginInfos($classname);
    $plIcon     = (isset($plInfo['plIcon'])?$plInfo['plIcon']:'plugin.png');
    $plHeadline = $plInfo['plTitle'];
    if ($objectType === FALSE) {
      $key = key($plInfo['plObjectType']);
      if (is_numeric($key)) {
        $key = $plInfo['plObjectType'][$key];
      }
      $objectType = $key;
    }
    $plCategory = (isset($plInfo['plCategory'])?$plInfo['plCategory']:array('user'));
    $key = key($plCategory);
    if (is_numeric($key)) {
      $plCategory = $plCategory[$key];
    } else {
      $plCategory = $key;
    }

    $lock_msg = "";
    if ($edit_mode) {
      /* Remove locks created by this plugin */
      if ($remove_lock || (isset($_POST['edit_cancel']) && session::is_set('edit'))) {
        if (session::is_set($classname)) {
          del_lock($entry_dn);
        }
      }
    }

    /* Remove this plugin from session */
    if ($cleanup) {
      session::un_set($classname);
      session::un_set('edit');
    } else {
      /* Reset requested? */
      if ($edit_mode && isset($_POST['edit_cancel'])) {
        session::un_set($classname);
        session::un_set('edit');
      }

      /* Create tab object on demand */
      if (!session::is_set($classname) || (isset($_GET['reset']) && $_GET['reset'] == 1)) {
        try {
          $tabObject = objects::open($entry_dn, $objectType);
        } catch (NonExistingLdapNodeException $e) {
          $tabObject = objects::open('new', $objectType);
        }
        if ($edit_mode) {
          $tabObject->setNeedEditMode(TRUE);
        }
        if (!$tabs) {
          $tabObject->current = $classname;
        }
        if (($entry_dn != '') && ($entry_dn != 'new')) {
          $tabObject->set_acl_base($entry_dn);
        } else {
          $tabObject->set_acl_base($config->current['BASE']);
        }
        session::set($classname, $tabObject);
      }
      $tabObject = session::get($classname);

      /* save changes back to object */
      if (!$edit_mode || session::is_set('edit')) {
        $tabObject->save_object();
      }

      if ($edit_mode) {
        /* Enter edit mode? */
        if ((isset($_POST['edit'])) && (!session::is_set('edit'))) {
          /* Check locking */
          if ($locks = get_locks($entry_dn)) {
            session::set('back_plugin', $plug);
            session::set('LOCK_VARS_TO_USE', array("/^edit$/", "/^plug$/"));
            $lock_msg = gen_locked_message($locks, $entry_dn);
          } else {
            /* Lock the current entry */
            add_lock($entry_dn, $ui->dn);
            session::set('edit', TRUE);
          }
        }

        /* save changes to LDAP and disable edit mode */
        $info = "";
        if (isset($_POST['edit_finish'])) {
          /* Perform checks */
          $message = $tabObject->check();

          /* No errors, save object */
          if (count($message) == 0) {
            $tabObject->save();
            del_lock($entry_dn);
            session::un_set('edit');

            /* Remove from session */
            session::un_set($classname);
          } else {
            /* Errors found, show message */
            msg_dialog::displayChecks($message);
          }
        }
      } else {
        $info = "";
      }

      /* Execute formular */
      if ($edit_mode && $lock_msg) {
        $display = $lock_msg;
      } else {
        if ($tabs) {
          $display .= $tabObject->execute();
        } else {
          $display .= $tabObject->by_object[$classname]->execute();
        }
      }

      /* Store changes  in session */
      if (!$edit_mode || session::is_set('edit')) {
        session::set($classname, $tabObject);
      }

      /* Show page footer depending on the mode */
      $info = $entry_dn.'&nbsp;';
      if ($edit_mode && (!$tabObject->dialogOpened()) && empty($lock_msg)) {
        $display .= '<p class="plugbottom">';

        /* Are we in edit mode? */
        if (session::is_set('edit')) {
          $display .= '<input type="submit" name="edit_finish" style="width:80px" value="'.msgPool::okButton().'"/>'."\n";
          $display .= '&nbsp;';
          $display .= '<input type="submit" name="edit_cancel" value="'.msgPool::cancelButton().'"/>'."\n";
        } else {
          /* Only display edit button if there is at least one attribute editable */
          if (preg_match('/r/', $ui->get_permissions($entry_dn, $plCategory.'/'.$tabObject->current))) {
            $info .= '<div style="display:inline-block" class="optional"><img class="center" alt="information" '.
                      'src="geticon.php?context=status&amp;icon=dialog-information&amp;size=16"> '.
                      msgPool::clickEditToChange().'</div>';

            $display .= '<input type="submit" name="edit" value="'.msgPool::editButton().'"/>'."\n";
          }
          $display .= '<input type="hidden" name="ignore"/>'."\n";
        }
        $display .= "</p>\n";
      }

      /* Page header */
      if (!preg_match('/^geticon/', $plIcon)) {
        $plIcon = get_template_path($plIcon);
      }
      $display = print_header($plIcon, $plHeadline, $info).$display;
    }
  }

  /*
   * \brief Adapt from template, using 'dn'
   *
   * \param string $dn The DN
   *
   * \param array $skip A new array
   */
  function adapt_from_template($attrs, $skip = array())
  {
    $this->attrs = $attrs;

    /* Walk through attributes */
    foreach ($this->attributesAccess as $ldapName => &$attr) {
      /* Skip the ones in skip list */
      if (in_array($ldapName, $skip)) {
        continue;
      }
      /* Load values */
      $attr->loadValue($this->attrs);
    }
    unset($attr);

    /* Is Account? */
    $this->is_account = $this->is_this_account($this->attrs);
  }

  protected function attributeHaveChanged($field)
  {
    return $this->attributesAccess[$field]->hasChanged();
  }

  protected function attributeValue($field)
  {
    return $this->attributesAccess[$field]->getValue();
  }

  protected function attributeInitialValue($field)
  {
    return $this->attributesAccess[$field]->getInitialValue();
  }

  function foreignKeyUpdate ($field, $oldvalue, $newvalue, $source)
  {
    if (!isset($source['MODE'])) {
      $source['MODE'] = 'move';
    }
    $this->attributesAccess[$field]->foreignKeyUpdate($oldvalue, $newvalue, $source);
  }

  /*
   * Source is an array like this:
   * array(
   *  'CLASS' => class,
   *  'FIELD' => field,
   *  'DN'    => dn,
   *  'MODE'  => mode
   * )
   * mode being either 'copy' or 'move', defaults to 'move'
   */
  function foreignKeyCheck ($field, $value, $source)
  {
    return $this->attributesAccess[$field]->foreignKeyCheck($value, $source);
  }

  function deserializeValues($values, $checkAcl = TRUE) {
    foreach ($values as $name => $value) {
      if (isset($this->attributesAccess[$name])) {
        if (!$checkAcl || $this->acl_is_writeable($this->attributesAccess[$name]->getAcl())) {
          $this->attributesAccess[$name]->setValue($value);
        } else {
          return msgPool::permModify($this->dn, $name);
        }
      } else {
        return sprintf(_('Unknown field "%s"'), $name);
      }
    }
    return TRUE;
  }

  /* Returns TRUE if this attribute should be asked in the creation by template dialog */
  function showInTemplate($attr, $templateAttrs)
  {
    if (isset($templateAttrs[$attr])) {
      return FALSE;
    }
    return TRUE;
  }
}

?>
