<?php
// $Id$

/**
* @file
* This is the actions engine for executing stored actions.
*/

/**
 * Given the IDs of actions to perform, find out what the callbacks
 * for the actions are by querying the database. Then call each callback
 * using the function call $function('do', $object, $context, $a1, $2)
 * where $function is the name of a function written in compliance with
 * the action specification; that is, foo($object, $context). The $params
 * parameter is an array of stored parameters that have been previously
 * configured through the web using actions.module.
 *
 * @param $action_ids
 *   The ID of the action to perform. Can be a single action ID or an array
 *   of IDs. IDs of instances will be numeric; IDs of singletons will be
 *   function names.
 * @param $object
 *   Parameter that will be passed along to the callback. Typically the
 *   object that the action will act on; a node, user or comment object.
 * @param $context
 *   Parameter that will be passed along to the callback. $context is a
 *   keyed array containing extra information about what is currently
 *   happening at the time of the call. Typically $context['hook'] and
 *   $context['op'] will tell which hook-op combination resulted in this
 *   call to actions_do().
 * @param $a1
 *   Parameter that will be passed along to the callback.
 * @param $a2
 *   Parameter that will be passed along to the callback.
 *
 * @return
 *   An associative array containing the result of the function that
 *   performs the action, keyed on action ID.
 */
function actions_do($action_ids, $object = NULL, $context = NULL, $a1 = NULL, $a2 = NULL) {
  static $stack;
  $stack++;
  if ($stack > variable_get('actions_max_stack', 35)) {
    watchdog('actions', 'Stack overflow: too many calls to actions_do(). Aborting to prevent infinite recursion.', WATCHDOG_ERROR);
    return;
  }
  $actions = array();
  $available_actions = actions_list();
  $result = array();
  if (is_array($action_ids)) {
    $where = array();
    $where_values = array();
    foreach ($action_ids as $action_id) {
      if (is_numeric($action_id)) {
        $where[] = 'OR aid = %d';
        $where_values[] = $action_id;
      }
      elseif (isset($available_actions[$action_id])) {
        $actions[$action_id] = $available_actions[$action_id];
      }
    }

    // When we have action instances we must go to the database to
    // retrieve instance data.
    if ($where) {
      $where_clause = implode(' ', $where);
      // Strip off leading 'OR '.
      $where_clause = '('. strstr($where_clause, " ") .')';
      $result_db = db_query('SELECT * FROM {actions} WHERE '. $where_clause, $where_values);
      while ($action = db_fetch_object($result_db)) {
        $action_id = $action->action_id;
        $actions[$action_id] = $action->params ? unserialize($data->parameters) : array();
        $actions[$action_id]['callback'] = $action->callback;
        $actions[$action_id]['type'] = $action->type;
      }
    }

    // Fire actions, in no particular order.
    foreach ($actions as $action_id => $params) {
      if (is_numeric($action_id)) { // Configurable actions need parameters.
        $function = $params['callback'];
        $context = array_merge($context, $params);
        $result[$action_id] = $function($object, $context, $a1, $a2);
      }
      // Singleton action; $action_id is the function name.
      else {
        $result[$action_id] = $action_id($object, $context, $a1, $a2);
      }
    }
  }
  // Optimized execution of single action.
  else {
    // If it's a configurable action, retrieve stored parameters.
    if (is_numeric($action_ids)) {
      $action = db_fetch_object(db_query("SELECT * FROM {actions} WHERE aid = %d", $action_ids));
      $function = $action->callback;
      $context = array_merge($context, unserialize($action->parameters));
      $result[$action_ids] = $function($object, $context, $a1, $a2);
    }
    // Singleton action; $action_ids is the function name.
    else {
      $result[$action_ids] = $action_ids($object, $context, $a1, $a2);
    }
  }
  return $result;
}


/**
 * Discover all action functions by invoking hook_action_info().
 *
 * mymodule_action_info() {
 *   return array(
 *     'mymodule_functiondescription_action' => array(
 *       'type' => 'node',
 *       'description' => t('Save node'),
 *       'configurable' => FALSE,
 *       'hooks' => array(
 *         'nodeapi' => array('delete','insert','update', 'view'),
 *         'comment' => array('delete','insert','update', 'view'),
 *       )
 *     )
 *   );
 * }
 *
 * The description is used in presenting possible actions to the user for
 * configuration. The type is used to present these actions in a logical
 * grouping and to denote context. Some types are 'node', 'user', 'comment',
 * and 'system'. If an action is configurable it will provide form,
 * validation and submission functions. The hooks the action supports
 * are declared in the 'hooks' array.
 *
 * @return
 *   An associative array keyed on function name. The value of each key is
 *   an array containing information about the action, such as type of
 *   action and description of the action, e.g.,
 *
 *   $actions['actions_node_publish'] = ('description' => 'Publish a node' ... )
 */
function actions_list() {
  static $actions;
  if (isset($actions)) {
    return $actions;
  }

  $actions = module_invoke_all('action_info');
  drupal_alter('action_info', $actions);
  return $actions;
}

/**
 * Retrieve all action instances from the database.
 * Compare with actions_list() which gathers actions by
 * invoking hook_action_info(). The two are synchronized
 * by visiting /admin/build/actions (when actions.module is
 * enabled) which runs actions_synchronize().
 *
 * @return
 *   Associative array keyed by action ID. Each value is
 *   an associative array with keys 'callback', 'description',
 *   'type' and 'configurable'.
 */
function actions_get_all_actions() {
  $actions = array();
  $result = db_query("SELECT * FROM {actions}");
  while ($action = db_fetch_object($result)) {
    $actions[$action->aid] = array(
      'callback' => $action->callback,
      'description' => $action->description,
      'type' => $action->type,
      'configurable' => (bool) $action->parameters,
    );
  }
  return $actions;
}

/**
 * Create an associative array keyed by md5 hashes of function names.
 * Hashes are used to prevent actual function names from going out into
 * HTML forms and coming back.
 *
 * @param $actions
 *   An associative array with function names as keys and associative
 *   arrays with keys 'description', 'type', etc. as values. Generally
 *   the output of actions_list() or actions_get_all_actions() is given
 *   as input to this function.
 *
 * @return
 *   An associative array keyed on md5 hash of function name. The value of
 *   each key is an associative array of function, description, and type
 *   for the action.
 */
function actions_actions_map($actions) {
  $actions_map = array();
  foreach ($actions as $callback => $array) {
    $key = md5($callback);
    $actions_map[$key]['callback']     = isset($array['callback']) ? $array['callback'] : $callback;
    $actions_map[$key]['description']  = $array['description'];
    $actions_map[$key]['type']         = $array['type'];
    $actions_map[$key]['configurable'] = $array['configurable'];
  }
  return $actions_map;
}

/**
 * Given an md5 hash of a function name, return the function name.
 * Faster than actions_actions_map() when you only need the function name.
 *
 * @param $hash
 *   MD5 hash of a function name
 *
 * @return
 *   Function name
 */
function actions_function_lookup($hash) {
  $actions_list = actions_list();
  foreach ($actions_list as $function => $array) {
    if (md5($function) == $hash) {
      return $function;
    }
  }

  // Must be an instance; must check database.
  $aid = db_result(db_query("SELECT aid FROM {actions} WHERE MD5(aid) = '%s' AND parameters != ''", $hash));
  return $aid;
}